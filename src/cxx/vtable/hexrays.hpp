#pragma once

#include "db/segments.hpp"
#include "decomp/ctree.hpp"
#include "decomp/analysis.hpp"
#include "decomp/stores.hpp"
#include "layout.hpp"
#include <hexrays.hpp>
#include <map>
#include <set>
#include <vector>

// Vtables in decompiled code: stores in constructors and virtual calls
namespace rv::vtbl {

/*
  &Plugin::`vftable'            (MSVC)
  &off_1234                    (stripped, address point already labeled)
  (char *)&`vtable for'X + 16  (Itanium: offset-to-top and RTTI come first)
*/
ea_t ref(cexpr_t* e);

// Stores of a vtable into an object: *(this + off) = vtable
struct store_finder_t : public ctree_visitor_t {
	struct hit_t {
		sval_t this_offset;
		ea_t vtable_ea;
		int var_idx;
		ea_t ctor_ea = BADADDR; // set by callers that search constructors
	};

	std::vector<hit_t> hits; // in source visit order

	store_finder_t() : ctree_visitor_t(CV_FAST) {}

	int idaapi visit_expr(cexpr_t* e) override {
		if (e->op != cot_asg) return 0;

		const ea_t vt = ref(e->y);
		if (vt == BADADDR) return 0;

		loc_t l;
		int sz;
		if (!fold_lvalue(e->x, nullptr, l, sz)) return 0;

		hits.push_back({ sval_t(l.off), vt, l.var });
		return 0;
	}
};

/*
	Vptr stores into var by subobject offset, the last store per offset wins.
	Only valid for constructors: base constructors store first and the class
	overwrites. Destructors store in the opposite order.
*/
std::map<sval_t, ea_t> ctor_vtables(cfunc_t* cf, int var);

// The last vtable stored at this+0 is function constructs
ea_t ctor_vtable(cfunc_t* cf, int this_var);

// Function in a vtable slot, through thunks
inline ea_t slot_func(ea_t vt, int slot) {
	if (vt == BADADDR || slot < 0) return BADADDR;
	const ea_t t = read_slot(vt, size_t(slot));
	return is_code_addr(t) ? resolve_thunk(t) : BADADDR;
}

} // namespace rv::vtbl

namespace rv {

// Looks for direct calls whose first argument is a local or &local
// (potential constructors)
struct ctor_candidate_finder_t : public ctree_visitor_t {
	std::vector<ea_t> candidates;

	ctor_candidate_finder_t() : ctree_visitor_t(CV_FAST) {}

	int idaapi visit_expr(cexpr_t* e) override {
		if (e->op != cot_call) return 0;

		cexpr_t* callee = strip_casts(e->x);
		if (callee->op != cot_obj) return 0;

		carglist_t* args = e->a;
		if (!args || args->empty()) return 0;

		cexpr_t* a0 = strip_casts(&args->at(0));
		if (a0->op == cot_ref) a0 = strip_casts(a0->x);
		if (a0->op != cot_var) return 0;

		candidates.push_back(callee->obj_ea);
		return 0;
	}
};

/*
	Tracks the locals that hold one object (objvars), its vtable (vtvars) and
	methods loaded from it (fnvars), and finds virtual calls for it. Pointers
	reached through the fields (parts) are other objects, e.g. a
	wrapper's plugin member: calls on them are followed but the vtable doesnt
	matter. With vptrs filled, fields at those offsets are secondary vptrs of
	the same object, and calls through them use that vtable.
*/
struct obj_tracker_t {

	std::set<int> objvars;
	std::set<int> vtvars;
	std::map<int, int> fnvars; // locals holding vtable loaded methods
	std::set<int> parts;
	int src_var = -1; // object = *(src_var + src_off), e.g. effect->object
	int64 src_off = -1;

	ea_t vtable = BADADDR;
	std::map<sval_t, ea_t> vptrs; // optional: subobject offset -> vtable

	// vptr offsets of vtvars/fnvars, absent = 0 (primary)
	std::map<int, sval_t> var_vptr;

	bool is_object(cexpr_t* e) const {
		e = strip_casts(e);
		if (e == nullptr) return false;

		if (e->op == cot_var) return objvars.count(e->v.idx) != 0;

		return src_var >= 0 && is_load_of(e, src_var, src_off);
	}

	// *obj, obj->vfptr, a secondary vptr from vptrs or a local holding one
	bool vptr_of(cexpr_t* e, sval_t& off) const;

	bool is_vtable(cexpr_t* e) const {
		sval_t off;
		return vptr_of(e, off);
	}

	/*
		Pointer reached through the object: obj->field, *(obj + k), &obj->m,
		obj + k, recursively. Never the object or its vptrs (*obj).
	*/
	bool is_part(cexpr_t* e) const;

	// vX = obj / *obj / (*obj)[n] / obj->field call for every assignment
	void absorb(cexpr_t* asg);

	/*
		Slot called by a virtual call:
		(*(fn **)(*(_QWORD *)obj + 8))(obj, ...)	byte offset
		(*((fn **)*obj + 1))(obj, ...)				scaled pointer arithmetic
		v7 = *obj; v7[1](obj, ...)					index into a vtable local
		v8 = *(*obj + 8); v8(obj, ...)				method local
		obj->vfptr->member(...)						typed vtable
		The vtable itself must come from the object, which rejects plain
		function pointer fields such as obj->callback(). vptr_off receives the
		subobject offset of the vptr used (0 = primary).
	*/
	bool vcall_slot(
		cexpr_t* callee, int& slot, sval_t* vptr_off = nullptr) const;

	// Vtable of the subobject at off
	ea_t vtable_at(sval_t off) const {
		auto it = vptrs.find(off);
		if (it != vptrs.end()) return it->second;
		return off == 0 ? vtable : BADADDR;
	}

	// Virtual call target, BADADDR if not virtual
	ea_t vcall_target(cexpr_t* callee) const {
		int slot;
		sval_t off;
		return vcall_slot(callee, slot, &off)
				   ? vtbl::slot_func(vtable_at(off), slot)
				   : BADADDR;
	}
};

} // namespace rv

namespace rv::vtbl {

/*
	Classes constructed on top of ctor_ea: callers that pass their own this
	to it and then store a vtable of their own are derived constructors. The
	ends of these chains are the most derived classes; there are several in
	shell plugins.
*/
void ctor_chain_leaves(
	ea_t ctor_ea, int depth, std::set<ea_t>& seen, std::vector<ea_t>& out);

inline std::vector<ea_t> most_derived(ea_t ctor_ea, int depth = 4) {
	std::vector<ea_t> out;
	std::set<ea_t> seen;
	ctor_chain_leaves(ctor_ea, depth, seen, out);
	return out;
}

} // namespace rv::vtbl
