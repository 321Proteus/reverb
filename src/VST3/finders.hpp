#pragma once
#include <bytes.hpp>
#include <hexrays.hpp>
#include <ida.hpp>
#include <segment.hpp>
#include <vector>

// Executable-segment check instead of get_func()/is_code(): vtable targets
// haven't been analyzed yet when we run
static bool is_exec_ea(ea_t ea) {
	segment_t* s = getseg(ea);
	return s != nullptr &&
		   ((s->perm & SEGPERM_EXEC) != 0 || s->type == SEG_CODE);
}

static int vtable_run_length(ea_t start_ea, int max_entries = 64) {
	const int ptrsz = inf_is_64bit() ? 8 : 4;
	int count = 0;

	for (int i = 0; i < max_entries; i++) {

		ea_t slot = start_ea + i * ptrsz;
		if (!is_loaded(slot)) break;

		ea_t target = ptrsz == 8 ? get_qword(slot) : get_dword(slot);
		if (target == 0 || !is_exec_ea(target)) break;
		count++;
	}

	return count;
}

static bool looks_like_vtable(ea_t ea) { return vtable_run_length(ea) >= 2; }

struct vtable_store_finder_t : public ctree_visitor_t {
	typedef struct {
		sval_t this_offset;
		ea_t vtable_ea;
		int var_idx;
		ea_t ctor_ea = BADADDR;
	} hit_t;

	std::vector<hit_t> hits;
	vtable_store_finder_t() : ctree_visitor_t(CV_FAST) {}

	int idaapi visit_expr(cexpr_t* e) override {

		if (e->op != cot_asg) return 0;

		cexpr_t* rhs = e->y;
		if (rhs->op == cot_ref) rhs = rhs->x;
		if (rhs->op != cot_obj)
			return 0; // must be a direct ref to a global address

		ea_t candidate_ea = rhs->obj_ea;
		if (!looks_like_vtable(candidate_ea)) return 0;

		cexpr_t* lhs = e->x;
		if (lhs->op != cot_ptr) // must be a dereference *(...)
			return 0;

		cexpr_t* addr = lhs->x;
		while (addr->op == cot_cast)
			addr = addr->x;

		sval_t offset = 0;
		cexpr_t* base = addr;
		if (addr->op == cot_add) {
			cexpr_t *l = addr->x, *r = addr->y;
			if (r->op == cot_num) {
				base = l;
				offset = r->numval();
			} else if (l->op == cot_num) {
				base = r;
				offset = l->numval();
			} else
				return 0;
		}
		while (base->op == cot_cast)
			base = base->x;

		if (base->op != cot_var) return 0;

		hits.push_back({ offset, candidate_ea, base->v.idx });
		return 0;
	}
};

inline bool label_vtable(ea_t vtbl_ea, const char* type_name) {
	tinfo_t ti;
	if (!ti.get_named_type(nullptr, type_name)) return false;

	size_t sz = ti.get_size();
	if (sz == BADSIZE) return false;

	del_items(vtbl_ea, DELIT_EXPAND, sz);

	return apply_tinfo(vtbl_ea, ti, TINFO_DEFINITE);
}

struct global_assign_finder_t : public ctree_visitor_t {
	int target_var_idx;
	ea_t global_ea = BADADDR;

	global_assign_finder_t(int idx)
		: ctree_visitor_t(CV_FAST), target_var_idx(idx) {}

	int idaapi visit_expr(cexpr_t* e) override {
		if (e->op != cot_asg) return 0;

		cexpr_t* lhs = e->x;
		if (lhs->op != cot_obj) return 0;

		cexpr_t* rhs = e->y;
		while (rhs->op == cot_cast)
			rhs = rhs->x;
		if (rhs->op != cot_var || rhs->v.idx != target_var_idx) return 0;

		global_ea = lhs->obj_ea;
		return 1;
	}
};

struct ctor_candidate_finder_t : public ctree_visitor_t {
	std::vector<ea_t> candidates;

	ctor_candidate_finder_t() : ctree_visitor_t(CV_FAST) {}

	int idaapi visit_expr(cexpr_t* e) override {
		if (e->op != cot_call) return 0;

		cexpr_t* callee = e->x;
		while (callee->op == cot_cast)
			callee = callee->x;
		if (callee->op != cot_obj) return 0;

		carglist_t* args = e->a;
		if (!args || args->empty()) return 0;

		cexpr_t* arg0 = &args->at(0);
		while (arg0->op == cot_cast)
			arg0 = arg0->x;

		if (arg0->op != cot_var) return 0;

		candidates.push_back(callee->obj_ea);
		return 0;
	}
};

struct ctor_global_finder_t : public ctree_visitor_t {
	ea_t ctor_ea;
	ea_t global_ea = BADADDR;
	int result_var_idx = -1;

	ctor_global_finder_t(ea_t ctor) : ctree_visitor_t(CV_FAST), ctor_ea(ctor) {}

	static cexpr_t* strip_casts(cexpr_t* e) {
		while (e->op == cot_cast)
			e = e->x;
		return e;
	}

	bool is_call_to_ctor(cexpr_t* e) {
		e = strip_casts(e);
		if (e->op != cot_call) return false;
		cexpr_t* callee = strip_casts(e->x);
		return callee->op == cot_obj && callee->obj_ea == ctor_ea;
	}

	int idaapi visit_expr(cexpr_t* e) override {
		if (e->op != cot_asg) return 0;

		cexpr_t* rhs = strip_casts(e->y);

		// step 1: "v5 = ctor(...)" — remember which local caught the return
		// value
		if (is_call_to_ctor(rhs)) {
			cexpr_t* lhs = strip_casts(e->x);
			if (lhs->op == cot_var) result_var_idx = lhs->v.idx;
		}

		// step 2: "global = v5" (or "global = ctor(...)" directly, if inlined
		// further)
		cexpr_t* lhs = strip_casts(e->x);
		if (lhs->op != cot_obj) return 0;

		if (is_call_to_ctor(rhs) ||
			(rhs->op == cot_var && rhs->v.idx == result_var_idx))
			global_ea = lhs->obj_ea;

		return 0;
	}
};