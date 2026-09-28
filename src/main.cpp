#include "common/analysis.hpp"
#include "common/module.hpp"
#include "reverb.hpp"
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
#include "types_embed.hpp"
#include "util.hpp"

static void apply_interface(rv::Interface iface) {

	rv::set_detected_interface(iface);
	qstring name = rv::get_interface_name(iface);
	rv_log("Plugin interface %s detected", name.c_str());

	const char* decl_src = rv::get_types_decl(iface);
	if (decl_src == nullptr)
		rv_log("No type declarations compiled in for %s", name.c_str());
	else
		parse_decls(nullptr, decl_src, msg, 0);
}

static ssize_t idaapi ui_event_callback(void* user_data, int notification_code,
										va_list va) {
	if (notification_code != ui_database_inited) return 0;

	rv_log("Database init complete - scanning");

	std::vector<qstring> exports;
	ea_t main_ea = BADADDR;
	int n = get_entry_qty();

	for (int i = 0; i < n; i++) {

		uval_t ord = get_entry_ordinal(i);
		ea_t ea = get_entry(ord);
		qstring name;

		get_entry_name(&name, ord);
		if (name == "main") main_ea = ea;
		exports.push_back(name);

		// rv_log("Index: %llu, Ordinal: %llu, Address: %a, Name: %s", i,
		// ord, ea, name.c_str());
	}

	rv::Interface iface = classify_from_exports(exports);

	if (iface == rv::OTHER && main_ea != BADADDR) {
		rv_log("Ambiguous main export - analyzing on demand");
		if (rv::ensure_func(main_ea) != nullptr && check_vst2_ep(main_ea))
			iface = rv::VST2;
	}

	if (iface == rv::OTHER) {
		rv_log("No supported plugin interface found, stopping");
		return 0;
	}

	apply_interface(iface);

	const rv::FormatModule* mod = rv::get_format_module(iface);
	if (mod != nullptr && mod->label != nullptr) mod->label();

	return 0;
}

static plugmod_t* idaapi init(void) {
	if (!init_hexrays_plugin()) {
		rv_log("Failed to initialize Hex-Rays plugin, skipping");
		return PLUGIN_SKIP;
	}

	hook_to_notification_point(HT_UI, ui_event_callback, nullptr);
	rv_log("plugin initialized");

	return PLUGIN_KEEP;
}

static void idaapi term(void) {
	unhook_from_notification_point(HT_UI, ui_event_callback);
	term_hexrays_plugin();
}

static bool idaapi run(size_t) {
	rv_log("manually invoked");
	return true;
}

plugin_t PLUGIN = {IDP_INTERFACE_VERSION,
				   0,
				   init,
				   term,
				   run,
				   "REverb - the virtual instrument RE toolkit",
				   "",
				   "reverb",
				   ""};