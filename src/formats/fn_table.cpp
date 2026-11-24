#include "fn_table.hpp"
#include "core/log.hpp"
#include "db/read.hpp"
#include "db/segments.hpp"
#include "db/types.hpp"
#include "decomp/analysis.hpp"
#include "decomp/stores.hpp"
#include <bytes.hpp>
#include <climits>
#include <funcs.hpp>
#include <name.hpp>
#include <segment.hpp>

namespace rv {

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
		if (!xb.iscode && in_metadata_seg(xb.from)) continue;
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
		cmt.sprnt("REverb: %s (shared function, left unnamed)", name.c_str());
		set_func_cmt(get_func(fn), cmt.c_str(), true);
		return;
	}

	// One function behind several members (a shared no-op): keep the first
	// name, comment the other roles
	qstring cur;
	get_ea_name(&cur, fn);
	if (cur != name && cur.starts_with(qstring(prefix) + "__")) {
		qstring cmt, also;
		get_func_cmt(&cmt, get_func(fn), true);
		also.sprnt("also %s", name.c_str());
		if (!strstr(cmt.c_str(), also.c_str())) {
			if (!cmt.empty()) cmt += "\n";
			set_func_cmt(get_func(fn), (cmt + also).c_str(), true);
		}
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

bool is_fn_table(ea_t ea, const tinfo_t& tif, bool allow_null) {
	const size_t sz = tif.get_size();
	if (sz == BADSIZE || !is_loaded(ea) || !is_loaded(ea + sz - 1) ||
		in_got_seg(ea))
		return false;

	udt_type_data_t udt;
	if (!tif.get_udt_details(&udt)) return false;

	int fns = 0;
	for (const udm_t& m : udt) {
		const ea_t fn = read_ptr(ea + m.offset / 8);
		if (!m.type.is_funcptr()) {
			if (m.type.is_ptr() && is_code_addr(fn)) return false; // desc
			continue;
		}
		if (fn == 0 && allow_null) continue;
		if (!is_code_addr(fn) || in_extern_seg(fn)) return false;
		fns++;
	}
	return fns > 0;
}

bool resolve_table(ea_t& ea, const tinfo_t& tif, bool allow_null) {
	if (is_fn_table(ea, tif, allow_null)) return true;
	ea_t p;
	if (!load_ptr_const(ea, p) || !is_fn_table(p, tif, allow_null))
		return false;
	ea = p;
	return true;
}

qstring c_ident(const qstring& s) {
	qstring r = s;
	for (size_t i = 0; i < r.length(); i++)
		if (uchar(r[i]) > CHAR_MAX || !qisalnum(r[i])) r[i] = '_';
	return r;
}

} // namespace rv
