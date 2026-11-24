#pragma once
#include "decomp/ctree.hpp"
#include "cxx/vtable.hpp"
#include <hexrays.hpp>

/**
 * @file finders.hpp
 * @brief Ctree visitors locating the globals a factory is stored in.
 * @internal Not part of the public API; used within the VST3 module only.
 */
/** @cond INTERNAL */

/** @brief Finds the global that the local `target_var_idx` is assigned to */
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

/**
 * @brief Finds the global that holds the object built by the constructor
 * @c ctor_ea: `v5 = ctor(...); global = v5;`
 */
struct ctor_global_finder_t : public ctree_visitor_t {
	ea_t ctor_ea;
	ea_t global_ea = BADADDR;

	std::set<int> obj_vars;

	ctor_global_finder_t(ea_t ctor) : ctree_visitor_t(CV_FAST), ctor_ea(ctor) {}

	bool is_call_to_ctor(cexpr_t* e) {
		e = strip_casts(e);
		if (e->op != cot_call) return false;

		cexpr_t* callee = strip_casts(e->x);
		return callee->op == cot_obj && callee->obj_ea == ctor_ea;
	}

	int idaapi visit_expr(cexpr_t* e) override {

		if (e->op == cot_call && is_call_to_ctor(e) && e->a != nullptr &&
			e->a->size() > 0) {

			cexpr_t* a0 = strip_casts(&e->a->at(0));
			if (a0->op == cot_var) obj_vars.insert(a0->v.idx);

			return 0;
		}

		if (e->op != cot_asg) return 0;

		cexpr_t* rhs = strip_casts(e->y);

		// step 1: "v5 = ctor(...)"
		if (is_call_to_ctor(rhs)) {
			cexpr_t* lhs = strip_casts(e->x);
			if (lhs->op == cot_var) obj_vars.insert(lhs->v.idx);
		}

		// step 2: "global = v5" (or "global = ctor(...)" directly)
		cexpr_t* lhs = strip_casts(e->x);
		if (lhs->op != cot_obj) return 0;

		if (is_call_to_ctor(rhs) ||
			(rhs->op == cot_var && obj_vars.count(rhs->v.idx)))
			global_ea = lhs->obj_ea;

		return 0;
	}
};
/** @endcond */
