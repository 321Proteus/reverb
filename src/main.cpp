#include "core/log.hpp"
#include "db/type_name.hpp"
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
#include "cxx/vtable/reconstruct.hpp"
#include "formats/types_embed.hpp"
#include "frameworks/framework.hpp"
#include "db/til/til.hpp"
#include "frameworks/til.hpp"
#include "formats/vst2/vst2.hpp"
#include "ui/panel.hpp"
#include "ui/report.hpp"
#include "db/report.hpp"
#include "debugger/launch.hpp"

/**
 * @brief Find and load the TIL library containing the plugin format types
 *
 * @param import whether or not to load the types into IDA local types
 * @sa rv::til::Request
 * @return true if the TIL load succeeded
 */
static bool format_til(bool import) {

	rv::til::Request req{ "Format", "formats" };

	req.import_types = import;
	return rv::til::load(req);
}

static void load_types(rv::Interface iface, bool from_til) {
	if (rv::get_loaded_types().has(iface)) return;

	if (!from_til) {
		const char* decl_src = rv::get_types_decl(iface);

		if (decl_src == nullptr) {
			rv_log("No type declarations compiled in for %s",
				rv::get_interface_name(iface));
			return;
		}

		const uint32 first = get_ordinal_limit();

		parse_decls(nullptr, rv::prepare_types_decl(decl_src).c_str(), msg, 0);

		rv::reset_type_cache();

		rv::til::group_local_types(
			first, get_ordinal_limit(), rv::get_interface_name(iface));
	}

	rv::mark_types_loaded(iface);
}

static void apply_interfaces(rv::InterfaceSet is, bool from_til) {

	rv::set_detected_interfaces(is);

	is.for_each([from_til](rv::Interface iface) {
		rv_log("Plugin interface %s detected", rv::get_interface_name(iface));
		load_types(iface, from_til);
	});
}

static void report_overview(rv::InterfaceSet is) {
	qstring ifaces;
	is.for_each([&](rv::Interface iface) {
		if (!ifaces.empty()) ifaces += ", ";
		ifaces += rv::get_interface_name(iface);
	});

	const rv::FrameworkInfo fw = rv::get_detected_framework();
	qstring f =
		fw.score > 0 ? rv::get_framework_name(fw.fw) : "(none detected)";
	if (!fw.version.empty()) f.cat_sprnt(" %s", fw.version.c_str());

	rv::report::heading("Overview");
	rv::report::row("interfaces", ifaces);
	rv::report::row("framework", f);
}

/**
 * @brief Scan pending: on the first auto_empty_finally for a new database, or
 * the first auto_empty for a reopened one (it has no final pass left to run)
 *
 */
enum class pending_t { NONE, FINAL, EMPTY };
static pending_t g_pending = pending_t::NONE;

static void run_scan(rv::InterfaceSet is) {
	g_pending = pending_t::NONE;

	// skip non-plugin binaries
	if (is.empty()) return;

	rv::report::clear();
	report_overview(is);

	is.for_each([](const rv::Interface iface) {
		const rv::FormatModule* mod = rv::get_format_module(iface);
		if (!mod) return;

		if (mod->scan != nullptr) mod->scan();
		if (mod->label != nullptr) mod->label();
	});

	rv::vtbl::reconstruct();
	rv::store_report(rv::report::entries());
	rv::panel::show_later();
}

/** @brief Classify the format(s) and framework + load types */
static rv::InterfaceSet detect() {
	std::vector<qstring> exports;
	int n = get_entry_qty();

	for (int i = 0; i < n; i++) {
		uval_t ord = get_entry_ordinal(i);
		qstring name;

		get_entry_name(&name, ord);
		exports.push_back(name);
	}

	rv::InterfaceSet formats = classify_from_exports(exports);
	ea_t main_ea = get_name_ea(BADADDR, "main");

	if (formats.empty() && main_ea != BADADDR) {
		rv_log("Ambiguous main export, probing for VST2");
		if (!format_til(false)) load_types(rv::Interface::VST2, false);
		if (rv::vst2::check_vst2_ep(main_ea)) formats.add(rv::Interface::VST2);
	}

	bool from_til = false;
	if (!formats.empty()) from_til = format_til(true);
	apply_interfaces(formats, from_til);

	rv::set_detected_framework(rv::scan_framework());
	rv::apply_framework_types(formats, rv::get_detected_framework());

	if (!formats.empty()) rv::debug::configure_minihost(formats);

	return formats;
}

static ssize_t idaapi ui_event_cb(
	void* user_data, int notification_code, va_list va) {

	if (notification_code == ui_database_closed) {
		g_pending = pending_t::NONE;
		rv::reset_type_cache();
		rv::panel::close();
		rv::report::clear();
		return 0;
	}

	if (notification_code != ui_database_inited) return 0;
	const bool is_new = va_arg(va, int) != 0;

	rv_log("Database init complete - scanning");

	const rv::InterfaceSet formats = detect();

	if (formats.empty()) {
		rv_log("No supported plugin interface found, stopping");
		return 0;
	} else {
		std::vector<rv::report::report_entry_t> stored;

		if (!is_new && rv::load_report(stored)) {
			rv::report::assign(std::move(stored));
			rv_log("Loaded the plugin info saved in the database");

		} else if (auto_is_ok()) {
			rv_log("Analysis already performed, scanning now");
			run_scan(formats);

		} else {
			g_pending = is_new ? pending_t::FINAL : pending_t::EMPTY;
			rv_log(
				"The metadata scan will run once the IDA "
				"analysis is finished. You can start browsing the file now.");
		}
	}

	return 0;
}

static ssize_t idaapi idb_event_cb(
	void* user_data, int notification_code, va_list va) {

	const bool ready = (notification_code == idb_event::auto_empty_finally &&
						   g_pending != pending_t::NONE) ||
					   (notification_code == idb_event::auto_empty &&
						   g_pending == pending_t::EMPTY);

	if (!ready) return 0;

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
	rv::panel::init();
	rv::debug::init();

	rv_log("plugin initialized");

	return PLUGIN_KEEP;
}

static void idaapi term(void) {
	rv::debug::term();
	rv::panel::term();
	unhook_from_notification_point(HT_UI, ui_event_cb);
	unhook_from_notification_point(HT_IDB, idb_event_cb);
	term_hexrays_plugin();
}

// The Edit section button rescans the binary
static bool idaapi run(size_t) {
	const rv::InterfaceSet formats = detect();

	if (formats.empty()) {
		rv_log("No supported plugin interface found, nothing to scan");
		return true;
	}

	if (auto_is_ok()) {
		rv_log("Rescanning");
		run_scan(formats);
		return true;
	}

	if (g_pending == pending_t::NONE) g_pending = pending_t::EMPTY;
	rv_log("The rescan will run once the IDA analysis is finished");

	return true;
}

plugin_t PLUGIN = { IDP_INTERFACE_VERSION, 0, init, term, run,
	"REverb - the virtual instrument RE toolkit", "", "reverb", "" };