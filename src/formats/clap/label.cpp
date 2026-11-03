#include "label.hpp"
#include "core/log.hpp"
#include "db/read.hpp"
#include "db/types.hpp"
#include "decomp/analysis.hpp"
#include <bytes.hpp>
#include <climits>
#include <funcs.hpp>
#include <name.hpp>
#include <segment.hpp>

namespace rv::clap {

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

fn_ref_t classify_fn(ea_t fn, ea_t owner, size_t owner_size) {
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

// A callback that only calls another function: name the callee name_impl
static void label_impl(ea_t fn, const qstring& name) {
	bool indirect = false;
	const ea_t impl = rv::direct_tail_callee(fn, &indirect);
	if (impl == BADADDR || impl == fn) {
		if (indirect)
			rv_log("%s at %a forwards through indirect call", name.c_str(), fn);
		return;
	}

	func_t* pfn = get_func(fn);
	const fn_ref_t r = classify_fn(impl, pfn->start_ea, pfn->size());
	if (r.shared || r.keep_name) return;

	set_name(impl, (name + "_impl").c_str(), SN_NOWARN | SN_FORCE);
	mark_cfunc_dirty(impl);
}

void label_fn(ea_t fn, const udm_t& m, const char* prefix, ea_t owner,
	size_t owner_size) {
	if (rv::ensure_func(fn) == nullptr) return;

	const fn_ref_t r = classify_fn(fn, owner, owner_size);
	qstring name;
	name.sprnt("%s__%s", prefix, m.name.c_str());

	if (r.shared) {
		qstring cmt;
		cmt.sprnt("CLAP: %s (shared function, left unnamed)", name.c_str());
		set_func_cmt(get_func(fn), cmt.c_str(), true);
		return;
	}

	apply_tinfo(fn, m.type.get_pointed_object(), TINFO_DEFINITE);
	if (!r.keep_name && !set_name(fn, name.c_str(), SN_NOWARN | SN_FORCE)) {
		name.replace("::", "__");
		set_name(fn, name.c_str(), SN_NOWARN | SN_FORCE);
	}
	get_ea_name(&name, fn); // the kept one, if any
	label_impl(fn, name);

	mark_cfunc_dirty(fn);
}

void label_fn_table(ea_t ea, const char* type, const char* prefix) {
	tinfo_t tif;
	if (!apply_type_at(ea, type, &tif)) return;

	udt_type_data_t udt;
	if (!tif.get_udt_details(&udt)) return;

	for (const udm_t& m : udt)
		if (m.type.is_funcptr())
			label_fn(
				read_ptr(ea + m.offset / 8), m, prefix, ea, tif.get_size());
}

qstring c_ident(const qstring& s) {
	qstring r = s;
	for (char& c : r)
		if (uchar(c) > CHAR_MAX || !qisalnum(c)) c = '_';
	return r;
}

} // namespace rv::clap
