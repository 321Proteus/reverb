#pragma once

#include "common/analysis.hpp"
#include "common/stores.hpp"
#include "util.hpp"
#include <bytes.hpp>
#include <demangle.hpp>
#include <hexrays.hpp>
#include <ida.hpp>
#include <nalt.hpp>
#include <name.hpp>
#include <set>
#include <vector>
#include <xref.hpp>

namespace rv {

// Pointers to executable in a row
inline int vtable_run_length(ea_t start_ea, int max_entries = 64) {
	const int psz = ptr_size();
	int count = 0;

	for (int i = 0; i < max_entries; i++) {
		ea_t slot = start_ea + i * psz;
		if (!is_loaded(slot)) break;

		ea_t target = read_ptr(slot);
		if (target == 0 || !in_exec_seg(target)) break;
		count++;
	}
	return count;
}

inline bool looks_like_vtable(ea_t ea) { return vtable_run_length(ea) >= 2; }

/*
  &Plugin::`vftable'            (MSVC)
  &off_1234                    (stripped, address point already labeled)
  (char *)&`vtable for'X + 16  (Itanium: offset-to-top and RTTI come first)
*/
inline ea_t vtable_ref(cexpr_t* e) {
	e = strip_casts(e);
	int64 off = 0;
	if (e->op == cot_add || e->op == cot_sub) {
		cexpr_t* n = strip_casts(e->y);
		if (n->op != cot_num) return BADADDR;
		off = scaled_off(e, int64(n->numval()));
		if (e->op == cot_sub) off = -off;
		e = strip_casts(e->x);
	}
	if (e->op == cot_ref) e = strip_casts(e->x);
	if (e->op != cot_obj) return BADADDR;

	const ea_t vt = e->obj_ea + off;
	return looks_like_vtable(vt) ? vt : BADADDR;
}

// Stores of a vtable into an object: *(this + off) = vtable
struct vtable_store_finder_t : public ctree_visitor_t {
	struct hit_t {
		sval_t this_offset;
		ea_t vtable_ea;
		int var_idx;
		ea_t ctor_ea = BADADDR;
	};

	std::vector<hit_t> hits; // in source visit order

	vtable_store_finder_t() : ctree_visitor_t(CV_FAST) {}

	int idaapi visit_expr(cexpr_t* e) override {
		if (e->op != cot_asg) return 0;

		const ea_t vt = vtable_ref(e->y);
		if (vt == BADADDR) return 0;

		loc_t l;
		int sz;
		if (!fold_lvalue(e->x, nullptr, l, sz)) return 0;

		hits.push_back({ sval_t(l.off), vt, l.var });
		return 0;
	}
};

// The last vtable stored at this+0 is function constructs
inline ea_t ctor_vtable(cfunc_t* cf, int this_var) {
	if (cf == nullptr || this_var < 0) return BADADDR;

	vtable_store_finder_t vf;
	vf.apply_to(&cf->body, nullptr);

	ea_t last = BADADDR;
	for (const auto& h : vf.hits)
		if (h.this_offset == 0 && h.var_idx == this_var) last = h.vtable_ea;
	return last;
}

inline bool label_vtable(ea_t vtbl_ea, const char* type_name) {
	tinfo_t ti;
	if (!ti.get_named_type(nullptr, type_name)) return false;

	size_t sz = ti.get_size();
	if (sz == BADSIZE) return false;

	del_items(vtbl_ea, DELIT_EXPAND, sz);

	return apply_tinfo(vtbl_ea, ti, TINFO_DEFINITE);
}

// Loks for direct calls whose first argument is a local (potential onstructors)
struct ctor_candidate_finder_t : public ctree_visitor_t {
	std::vector<ea_t> candidates;

	ctor_candidate_finder_t() : ctree_visitor_t(CV_FAST) {}

	int idaapi visit_expr(cexpr_t* e) override {
		if (e->op != cot_call) return 0;

		cexpr_t* callee = strip_casts(e->x);
		if (callee->op != cot_obj) return 0;

		carglist_t* args = e->a;
		if (!args || args->empty()) return 0;

		if (strip_casts(&args->at(0))->op != cot_var) return 0;

		candidates.push_back(callee->obj_ea);
		return 0;
	}
};

// Function in a vtable slot, through thunks
inline ea_t vtable_slot(ea_t vt, int slot) {
	if (vt == BADADDR || slot < 0) return BADADDR;
	const ea_t t = read_ptr(vt + ea_t(slot) * ptr_size());
	return is_code_addr(t) ? resolve_thunk(t) : BADADDR;
}

// Tracks the locals that hold one object (objvars) and its vtable (vtvars) and
// finds virtual calls for it
struct obj_tracker_t {

	std::set<int> objvars;
	std::set<int> vtvars;
	int src_var = -1; // object = *(src_var + src_off), e.g. effect->object
	int64 src_off = -1;

	bool is_object(cexpr_t* e) const {
		e = strip_casts(e);
		if (e == nullptr) return false;

		if (e->op == cot_var) return objvars.count(e->v.idx) != 0;

		return src_var >= 0 && is_load_of(e, src_var, src_off);
	}

	// *obj, obj->vfptr (primary vptr only) or a local holding either
	bool is_vtable(cexpr_t* e) const {
		e = strip_casts(e);
		if (e == nullptr) return false;

		if (e->op == cot_var) return vtvars.count(e->v.idx) != 0;
		if (e->op == cot_ptr) return is_object(e->x);
		if (e->op == cot_memptr) return e->m == 0 && is_object(e->x);

		return false;
	}

	// vX = obj / vX = *obj; call for every assignment
	void absorb(cexpr_t* asg) {
		if (asg->op != cot_asg) return;

		cexpr_t* l = strip_casts(asg->x);
		if (l->op != cot_var) return;

		if (is_object(asg->y)) objvars.insert(l->v.idx);
		else if (is_vtable(asg->y))
			vtvars.insert(l->v.idx);
	}

	/*
		Slot called by a virtual call:
		(*(fn **)(*(_QWORD *)obj + 8))(obj, ...)	byte offset
		(*((fn **)*obj + 1))(obj, ...)				scaled pointer arithmetic
		v7 = *obj; v7[1](obj, ...)					index into a vtable local
		obj->vfptr->member(...)						typed vtable
		The vtable itself must come from the object, which rejects plain
		function pointer fields such as obj->callback().
	*/
	bool vcall_slot(cexpr_t* callee, int& slot) const {
		cexpr_t* e = strip_casts(callee);
		cexpr_t* vt = nullptr;
		int64 off = 0;

		switch (e->op) {

		case cot_memptr:
			vt = e->x;
			off = e->m;
			break;

		case cot_idx: {
			cexpr_t* n = strip_casts(e->y);
			if (n->op != cot_num) return false;

			const int es = e->x->type.get_ptrarr_objsize();
			off = int64(n->numval()) * (es > 0 ? es : ptr_size());
			vt = e->x;
			break;
		}

		case cot_ptr: {
			cexpr_t* a = strip_casts(e->x);

			if (a->op == cot_add) {
				cexpr_t* n = strip_casts(a->y);
				if (n->op != cot_num) return false;

				off = scaled_off(a, int64(n->numval()));
				a = a->x;
			}

			vt = a;
			break;
		}

		default:
			return false;
		}

		if (!is_vtable(vt)) return false;

		const int psz = ptr_size();
		if (off < 0 || off % psz != 0) return false;

		slot = int(off / psz);
		return true;
	}
};

/*
	Classes constructed on top of ctor_ea: callers that pass their own this
	to it and then store a vtable of their own are derived constructors. The
	ends of these chains are the most derived classes; there are several in
	shell plugins.
*/
inline void ctor_chain_leaves(
	ea_t ctor_ea, int depth, std::set<ea_t>& seen, std::vector<ea_t>& out) {
	constexpr int MAX_CALLERS = 32;

	if (!seen.insert(ctor_ea).second) return;
	cfuncptr_t cf = decompile_at(ctor_ea);
	if (cf == nullptr || cf->argidx.empty()) return;

	const ea_t own = ctor_vtable(cf, cf->argidx[0]);
	bool derived = false;

	xrefblk_t xb;
	int n = 0;

	for (bool ok = xb.first_to(ctor_ea, XREF_NOFLOW);
		ok && depth > 0 && n < MAX_CALLERS; ok = xb.next_to()) {

		if (!xb.iscode || (xb.type != fl_CN && xb.type != fl_CF)) continue;

		func_t* f = get_func(xb.from);
		if (f == nullptr || seen.count(f->start_ea) != 0) continue;

		n++;

		cfuncptr_t caller = decompile_at(f->start_ea);
		if (caller == nullptr || caller->argidx.empty()) continue;

		const int self = caller->argidx[0];

		// ctor(this, ...) with own unadjusted ptr
		struct call_finder_t : public ctree_visitor_t {
			ea_t target;
			int self;
			bool found = false;

			call_finder_t(ea_t t, int s)
				: ctree_visitor_t(CV_FAST), target(t), self(s) {}

			int idaapi visit_expr(cexpr_t* e) override {
				if (e->op != cot_call || e->a == nullptr || e->a->empty())
					return 0;

				cexpr_t* fn = strip_casts(e->x);
				if (!fn) return 0;
				loc_t l;

				if (fn->op == cot_obj && fn->obj_ea == target &&
					fold_addr(&e->a->at(0), nullptr, l) && l.var == self &&
					l.off == 0)

					found = true;

				return found;
			}

		} cfind(ctor_ea, self);
		cfind.apply_to(&caller->body, nullptr);

		if (!cfind.found || ctor_vtable(caller, self) == BADADDR) continue;
		derived = true;
		ctor_chain_leaves(f->start_ea, depth - 1, seen, out);
	}

	if (!derived && own != BADADDR &&
		std::find(out.begin(), out.end(), own) == out.end()) {
		rv_log("Most derived ctor %a, vtable %a", ctor_ea, own);
		out.push_back(own);
	}
}

inline std::vector<ea_t> most_derived_vtables(ea_t ctor_ea, int depth = 4) {
	std::vector<ea_t> out;
	std::set<ea_t> seen;
	ctor_chain_leaves(ctor_ea, depth, seen, out);
	return out;
}

// Class name from the vtable RTTI, or empty
inline qstring rtti_class_name(ea_t vt) {
	const int psz = ptr_size();
	const ea_t meta = read_ptr(vt - psz);
	if (!is_loaded(meta)) return qstring();

	qstring raw;
	if (inf_get_filetype() == f_PE) {
		// COL signature 1 = relative offsets, 0 = pointers
		const uint32 sig = get_dword(meta);
		const ea_t td = sig == 1   ? get_imagebase() + get_dword(meta + 12)
						: sig == 0 ? ea_t(get_dword(meta + 12))
								   : BADADDR;

		if (td == BADADDR || !is_loaded(td)) return qstring();
		if (!read_cstr(td + 2 * psz, raw) || !raw.starts_with(".?A"))
			return qstring();

		// demangles, strip the suffix
		qstring dem;
		qstring mangled = qstring("??_R0") + raw.substr(1) + "@8";

		if (demangle_name(&dem, mangled.c_str(), MNG_SHORT_FORM) > 0) {
			size_t p = dem.find(" `RTTI");

			if (p != qstring::npos) dem.resize(p);
			if (dem.starts_with("class ")) dem.remove(0, 6);
			if (dem.starts_with("struct ")) dem.remove(0, 7);

			return dem;
		}

		return raw;
	}

	const ea_t name = read_ptr(meta + psz);

	if (!is_loaded(name) || !read_cstr(name, raw)) return qstring();
	if (raw.empty() || !(qisdigit(raw[0]) || raw[0] == 'N')) return qstring();

	qstring dem;
	qstring mangled = qstring("_Z") + raw;
	if (demangle_name(&dem, mangled.c_str(), MNG_SHORT_FORM) > 0) return dem;
	return raw;
}

} // namespace rv
