#include "clap.hpp"
#include "factory.hpp"
#include "descriptor.hpp"
#include "common/analysis.hpp"
#include "common/member.hpp"
#include "util.hpp"
#include <bytes.hpp>
#include <cstddef>
#include <name.hpp>
#include <pro.h>
#include <typeinf.hpp>

namespace rv::clap {

struct fn_ref_t {
	ea_t ea = BADADDR;
	bool keep_name = false;
	bool shared = false;
};

struct entry_info_t {
	ea_t ea = BADADDR;
	abi::clap_version_t version{};
	fn_ref_t init, deinit, get_factory;
};

static entry_info_t g_entry;
static bool g_valid = false;
static std::vector<desc_t> g_descs;

static ea_t find_entry() {
	for (const char* n : { "clap_entry", "_clap_entry" }) {
		const ea_t ea = get_name_ea(BADADDR, n);
		if (ea != BADADDR) return ea;
	}
	return BADADDR;
}

// Find nwind and exception table references
static bool is_metadata_ref(ea_t from) {
	segment_t* s = getseg(from);
	if (!s) return false;

	qstring n;
	get_segm_name(&n, s);

	static const char* const META_SECTIONS[] = {
		// PE64 unwind
		".pdata",
		".xdata",
		// ELF
		".eh_frame",
		".eh_frame_hdr",
		".gcc_except_table",
		// Mach O
		"__unwind_info",
		"__eh_frame",
		"__gcc_except_tab",
	};

	for (const char* m : META_SECTIONS)
		if (n == m) return true;
	return false;
}

// Referenced from anywhere other than [owner, owner + owner_size)?
static fn_ref_t classify_fn(ea_t fn, ea_t owner, size_t owner_size) {
	fn_ref_t r;
	r.ea = fn;
	r.keep_name = !has_dummy_name(get_flags(fn));

	func_t* f = get_func(fn);
	if (f != nullptr && (f->flags & FUNC_LIB) != 0) r.shared = true;

	xrefblk_t xb;
	for (bool ok = xb.first_to(fn, XREF_ALL); ok && !r.shared;
		ok = xb.next_to()) {
		if (xb.iscode && xb.type == fl_F) continue;
		if (xb.from >= owner && xb.from < owner + owner_size) continue;
		if (!xb.iscode && is_metadata_ref(xb.from)) continue;
		r.shared = true;
	}
	return r;
}

// Type the struct, then prototype and name every function member. Shared and
// already-named functions are commented and skipped.
static void label_fn_table(ea_t ea, const char* type, const char* prefix) {
	tinfo_t tif;
	if (!apply_type_at(ea, type, &tif)) return;

	udt_type_data_t udt;
	if (!tif.get_udt_details(&udt)) return;

	for (const udm_t& m : udt) {
		if (!m.type.is_funcptr()) continue;

		const ea_t fn = read_ptr(ea + m.offset / 8);
		if (rv::ensure_func(fn) == nullptr) continue;

		const fn_ref_t r = classify_fn(fn, ea, tif.get_size());
		qstring name;
		name.sprnt("%s__%s", prefix, m.name.c_str());

		if (r.shared) {
			qstring cmt;
			cmt.sprnt("CLAP: %s (shared function, left unnamed)", name.c_str());
			set_func_cmt(get_func(fn), cmt.c_str(), true);
			continue;
		}

		apply_tinfo(fn, m.type.get_pointed_object(), TINFO_DEFINITE);
		if (!r.keep_name && !set_name(fn, name.c_str(), SN_NOWARN | SN_FORCE)) {
			name.replace("::", "__");
			set_name(fn, name.c_str(), SN_NOWARN | SN_FORCE);
		}

		mark_cfunc_dirty(fn);
	}
}

// get_plugin_count usually just returns a constant
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
		g_descs.insert(g_descs.end(), descs.begin(), descs.end());
	}
	mark_cfunc_dirty(gf_ea);
}

void label() {}

} // namespace rv::clap