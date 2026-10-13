#pragma once

#include "common/analysis.hpp"
#include "common/stores.hpp"
#include "util.hpp"
#include <bytes.hpp>
#include <demangle.hpp>
#include <funcs.hpp>
#include <hexrays.hpp>
#include <ida.hpp>
#include <nalt.hpp>
#include <name.hpp>
#include <map>
#include <segment.hpp>
#include <set>
#include <typeinf.hpp>
#include <vector>
#include <xref.hpp>

namespace rv {

// Pointers to executable in a row
int vtable_run_length(ea_t start_ea, int max_entries = 64);

inline bool looks_like_vtable(ea_t ea) { return vtable_run_length(ea) >= 2; }

/*
  &Plugin::`vftable'            (MSVC)
  &off_1234                    (stripped, address point already labeled)
  (char *)&`vtable for'X + 16  (Itanium: offset-to-top and RTTI come first)
*/
ea_t vtable_ref(cexpr_t* e);

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
ea_t ctor_vtable(cfunc_t* cf, int this_var);

// Class name from the vtable RTTI, or empty
qstring rtti_class_name(ea_t vt);

tinfo_t find_class_type(const qstring& cls);

bool label_vtable(ea_t vtbl_ea, const char* type_name);

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

/*
	Tracks the locals that hold one object (objvars), its vtable (vtvars) and
	methods loaded from it (fnvars), and finds virtual calls for it. Pointers
	reached through the fields (parts) are other objects, e.g. a
	wrapper's plugin member: calls on them are followed but the vtable doesnt
   matter
*/
struct obj_tracker_t {

	std::set<int> objvars;
	std::set<int> vtvars;
	std::map<int, int> fnvars; // locals holding vtable loaded methods
	std::set<int> parts;
	int src_var = -1; // object = *(src_var + src_off), e.g. effect->object
	int64 src_off = -1;

	ea_t vtable = BADADDR;

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

	/*
		Pointer reached through the object: obj->field, *(obj + k), &obj->m,
		obj + k, recursively. Never the object or its vptr (*obj).
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
		function pointer fields such as obj->callback().
	*/
	bool vcall_slot(cexpr_t* callee, int& slot) const;

	// Virtual call target, BADADDR if not virtual
	ea_t vcall_target(cexpr_t* callee) const {
		int slot;
		return vcall_slot(callee, slot) ? vtable_slot(vtable, slot) : BADADDR;
	}
};

/*
	Classes constructed on top of ctor_ea: callers that pass their own this
	to it and then store a vtable of their own are derived constructors. The
	ends of these chains are the most derived classes; there are several in
	shell plugins.
*/
void ctor_chain_leaves(
	ea_t ctor_ea, int depth, std::set<ea_t>& seen, std::vector<ea_t>& out);

inline std::vector<ea_t> most_derived_vtables(ea_t ctor_ea, int depth = 4) {
	std::vector<ea_t> out;
	std::set<ea_t> seen;
	ctor_chain_leaves(ctor_ea, depth, seen, out);
	return out;
}

size_t count_vtable_slots(ea_t ea, size_t max);

const char* pick_vtable_type(ea_t ea, std::initializer_list<const char*> candidates);

} // namespace rv
