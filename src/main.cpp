#include "core/log.hpp"
#include "formats/format.hpp"
#include "core/reverb.hpp"
#include <auto.hpp>
#include <cstdio>
#include <hexrays.hpp>
#include <ida.hpp>
#include <idp.hpp>
#include <kernwin.hpp>
#include <loader.hpp>
#include <entry.hpp>
#include <name.hpp>
#include <pro.h>
#include <typeinf.hpp>
#include "detect.hpp"
#include "state.hpp"
#include "formats/types_embed.hpp"
#include "frameworks/framework.hpp"
#include "formats/vst2/vst2.hpp"

// A format probe may have loaded the types before detection ends
static void load_types(rv::Interface iface) {
	if (rv::get_loaded_types().has(iface)) return;

	const char* decl_src = rv::get_types_decl(iface);
	if (decl_src == nullptr) {
		rv_log("No type declarations compiled in for %s",
			rv::get_interface_name(iface));
		return;
	}
	parse_decls(nullptr, rv::prepare_types_decl(decl_src).c_str(), msg, 0);
	rv::mark_types_loaded(iface);
}

static void apply_interfaces(rv::InterfaceSet is) {

	rv::set_detected_interfaces(is);

	is.for_each([](rv::Interface iface) {
		rv_log("Plugin interface %s detected", rv::get_interface_name(iface));
		load_types(iface);
	});
}

static void run_scan(rv::InterfaceSet is) {
	is.for_each([](const rv::Interface iface) {
		const rv::FormatModule* mod = rv::get_format_module(iface);
		if (!mod) return;

		if (mod->scan != nullptr) mod->scan();
		if (mod->label != nullptr) mod->label();
	});
}

static ssize_t idaapi ui_event_cb(
	void* user_data, int notification_code, va_list va) {
	if (notification_code != ui_database_inited) return 0;

	rv_log("Database init complete - scanning");

	std::vector<qstring> exports;
	int n = get_entry_qty();

	for (int i = 0; i < n; i++) {

		uval_t ord = get_entry_ordinal(i);
		ea_t ea = get_entry(ord);
		qstring name;

		get_entry_name(&name, ord);
		exports.push_back(name);
	}

	rv::InterfaceSet formats = classify_from_exports(exports);
	ea_t main_ea = get_name_ea(BADADDR, "main");

	if (formats.empty() && main_ea != BADADDR) {
		rv_log("Ambiguous main export, probing for VST2");
		load_types(rv::Interface::VST2);
		if (rv::vst2::check_vst2_ep(main_ea)) formats.add(rv::Interface::VST2);
	}

	apply_interfaces(formats);
	rv::set_detected_framework(rv::scan_framework());

	if (formats.empty()) {
		rv_log("No supported plugin interface found, stopping");
		return 0;
	} else {
		if (auto_is_ok()) {
			rv_log("Analysis already performed, scanning now");
			run_scan(formats);
		} else {
			rv_log(
				"The metadata scan will run once the IDA "
				"analysis is finished. You can start browsing the file now.");
		}
	}

	return 0;
}

static ssize_t idaapi idb_event_cb(
	void* user_data, int notification_code, va_list va) {

	if (notification_code != idb_event::auto_empty_finally) return 0;

	run_scan(rv::get_detected_interfaces());

	return 0;
}

static plugmod_t* idaapi init(void) {
	if (!init_hexrays_plugin()) {
		rv_log("Failed to initialize Hex-Rays plugin, skipping");
		return PLUGIN_SKIP;
	}

	if (!inf_is_dll()) {
		rv_log("The program is not an executable, skipping");
		return PLUGIN_SKIP;
	}

	hook_to_notification_point(HT_UI, ui_event_cb, nullptr);
	hook_to_notification_point(HT_IDB, idb_event_cb);

	rv_log("plugin initialized");

	return PLUGIN_KEEP;
}

static void idaapi term(void) {
	unhook_from_notification_point(HT_UI, ui_event_cb);
	unhook_from_notification_point(HT_IDB, idb_event_cb);
	term_hexrays_plugin();
}

static bool idaapi run(size_t) {
	rv_log("manually invoked");
	return true;
}

plugin_t PLUGIN = { IDP_INTERFACE_VERSION, 0, init, term, run,
	"REverb - the virtual instrument RE toolkit", "", "reverb", "" };