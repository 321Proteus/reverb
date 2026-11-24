/**
 * @file scan.cpp
 * @brief VST3 scan.
 */
#include "core/log.hpp"
#include "decomp/analysis.hpp"
#include "factory.hpp"
#include "ui/report.hpp"
#include "vst3.hpp"
#include <cstring>

namespace rv::vst3 {

/**
 * @brief Names of the `PFactoryInfo` flags set in @p f, joined by ` | `, or
 * `none`
 */
static qstring factory_flags_str(uint64 f) {
	static const struct {
		uint64 bit;
		const char* name;
	} FLAGS[] = {
		{ 1 << 0, "ClassesDiscardable" },
		{ 1 << 1, "LicenseCheck" },
		{ 1 << 3, "ComponentNonDiscardable" },
		{ 1 << 4, "Unicode" },
	};

	qstring s;
	for (const auto& k : FLAGS)
		if (f & k.bit) {
			if (!s.empty()) s += " | ";
			s += k.name;
		}
	return s.empty() ? qstring("none") : s;
}

/**
 * @brief Adds the `vendor`, `url`, `email` and `flags` of the `PFactoryInfo` @p
 * s to the report
 */
static void report_factory_info(const factory_struct_t& s) {
	qstring v;
	for (const char* f : { "vendor", "url", "email" }) {
		report::row(f, str_field(s, f, v) && !v.empty() ? v : "(none)");
	}

	if (uint64 fl = 0; num_field(s, "flags", fl)) {
		report::row("flags", factory_flags_str(fl));
	}
}

void scan() {
	const ea_t gpf_ea = find_get_plugin_factory();
	report::heading("VST3 factory", gpf_ea);
	report::addr_row("GetPluginFactory", gpf_ea);

	if (gpf_ea == BADADDR) {
		rv_log("GetPluginFactory not found");
		return;
	}

	cfuncptr_t cf = decompile_at(gpf_ea);
	if (cf == nullptr) return;

	const auto structs = extract_factory_structs(cf);
	rv_log("Found %u factory structs in GetPluginFactory",
		unsigned(structs.size()));

	bool have_info = false;
	for (const factory_struct_t& s : structs) {
		if (s.type != nullptr && strcmp(s.type, "PFactoryInfo") == 0) {
			report_factory_info(s);
			have_info = true;
		}
	}

	if (!have_info) report::row("PFactoryInfo", "(not found)");

	label_classes(cf, structs);
}
} // namespace rv::vst3
