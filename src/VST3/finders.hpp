#pragma once
#include "common/vtable.hpp"
#include "util.hpp"
#include <hexrays.hpp>

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

struct ctor_global_finder_t : public ctree_visitor_t {
	ea_t ctor_ea;
	ea_t global_ea = BADADDR;
	int result_var_idx = -1;

	ctor_global_finder_t(ea_t ctor) : ctree_visitor_t(CV_FAST), ctor_ea(ctor) {}

	bool is_call_to_ctor(cexpr_t* e) {
		e = strip_casts(e);
		if (e->op != cot_call) return false;
		cexpr_t* callee = strip_casts(e->x);
		return callee->op == cot_obj && callee->obj_ea == ctor_ea;
	}

	int idaapi visit_expr(cexpr_t* e) override {
		if (e->op != cot_asg) return 0;

		cexpr_t* rhs = strip_casts(e->y);

		// step 1: "v5 = ctor(...)"
		if (is_call_to_ctor(rhs)) {
			cexpr_t* lhs = strip_casts(e->x);
			if (lhs->op == cot_var) result_var_idx = lhs->v.idx;
		}

		// step 2: "global = v5" (or "global = ctor(...)" directly)
		cexpr_t* lhs = strip_casts(e->x);
		if (lhs->op != cot_obj) return 0;

		if (is_call_to_ctor(rhs) ||
			(rhs->op == cot_var && rhs->v.idx == result_var_idx))
			global_ea = lhs->obj_ea;

		return 0;
	}
};