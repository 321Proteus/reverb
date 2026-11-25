/**
 * @file scan.cpp
 * @brief CLAP scan: `clap_entry` and what it leads to.
 */
#include "core/log.hpp"
#include "clap.hpp"
#include "factory.hpp"
#include "descriptor.hpp"
#include "label.hpp"
#include "plugin.hpp"
#include "decomp/analysis.hpp"
#include "decomp/returns.hpp"
#include "db/members.hpp"
#include "ui/report.hpp"
#include <bytes.hpp>
#include <cstddef>
#include <name.hpp>
#include <pro.h>
#include <typeinf.hpp>

namespace rv::clap {

/**
 * @brief `clap_entry` as read from the database
 */
struct entry_info_t {
	ea_t ea = BADADDR;
	version_t version{};
	fn_ref_t init, deinit, get_factory;
};

/**
 * @brief Entry found by the last scan()
 */
static entry_info_t g_entry;
/**
 * @brief Whether #g_entry holds a valid entry
 */
static bool g_valid = false;
/**
 * @brief Descriptors found by the last scan()
 */
static std::vector<desc_t> g_descs;

/**
 * @brief `clap_entry` by name, with or without the leading underscore
 */
static ea_t find_entry() {
	for (const char* n : { "clap_entry", "_clap_entry" }) {
		const ea_t ea = get_name_ea(BADADDR, n);
		if (ea != BADADDR) return ea;
	}
	return BADADDR;
}

/**
 * @brief Warns if `get_plugin_count` returns a constant that differs from the
 * @p found descriptors
 */
static void check_plugin_count(ea_t factory, size_t found) {
	const ea_t gpc =
		member_ptr(factory, "clap_plugin_factory_t", "get_plugin_count");
	uint64 n;
	if (gpc != BADADDR && rv::const_return(gpc, &n) && n != found)
		rv_log("get_plugin_count at %a returns %llu, but %u descriptor(s) "
			   "were found",
			gpc, (unsigned long long)n, unsigned(found));
}

void scan() {
	g_valid = false;
	g_entry = {};
	g_descs.clear();

	const ea_t ea = find_entry();
	if (ea == BADADDR) {
		rv_log("CLAP entry not found");
		return;
	}

	tinfo_t tif; // lookup only
	if (!tif.get_named_type(nullptr, "clap_plugin_entry_t")) {
		rv_log("CLAP types are not loaded");
		return;
	}
	const size_t sz = tif.get_size();
	if (sz == BADSIZE || !is_loaded(ea) || !is_loaded(ea + sz - 1)) {
		rv_log("clap_entry at %a is not backed by file data", ea);
		return;
	}

	label_fn_table(ea, "clap_plugin_entry_t", "clap_entry");

	report::heading("CLAP entry", ea);
	for (const char* m : { "init", "deinit", "get_factory" })
		report::addr_row(m, member_ptr(ea, tif, m));

	ea_t gf_ea = member_ptr(ea, tif, "get_factory");

	for (const factory_hit_t& f : find_factories(gf_ea)) {
		const bool plugin = strcmp(f.kind->name, "PLUGIN") == 0;
		std::vector<desc_t> descs;

		if (plugin) {
			descs = find_descriptors(
				member_ptr(f.ea, f.kind->type, "get_plugin_descriptor"));
			// an assumed plugin factory is confirmed by its descriptors
			if (f.assumed && descs.empty()) {
				rv_log("Unknown CLAP factory at %a", f.ea);
				continue;
			}
		}

		rv_log("Factory %s (\"%s\") at %a", f.kind->name, f.kind->id, f.ea);

		qstring prefix(f.kind->type);

		if (prefix.ends_with("_t")) prefix.remove_last(2);
		label_fn_table(f.ea, f.kind->type, prefix.c_str());

		if (has_dummy_name(get_flags(f.ea)))
			set_name(f.ea, prefix.c_str(), SN_NOWARN | SN_FORCE);

		if (!plugin) continue;
		for (const desc_t& d : descs)
			label_descriptor(d);

		check_plugin_count(f.ea, descs.size());

		// Typed const clap_plugin_t * by label_fn_table. C++ wrappers return
		// &plugin->clap_plugin, often from a create function it forwards to
		const ea_t create = member_ptr(f.ea, f.kind->type, "create_plugin");
		if (create != BADADDR && create != 0)
			rv::type_return_base(create, "create_plugin", 2);

		label_plugins(create, descs);
		g_descs.insert(g_descs.end(), descs.begin(), descs.end());
	}

	mark_cfunc_dirty(gf_ea);
}

void label() {}

} // namespace rv::clap