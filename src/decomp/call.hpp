#pragma once

#include "ctree.hpp"
#include <hexrays.hpp>
#include <vector>

namespace rv {

// One call argument
struct call_arg_t {
	enum kind_t { NONE, NUM, OBJ, VAR, STR } k = NONE;
	uint64 num = 0;	   // for NUM
	ea_t ea = BADADDR; // for OBJ: the global's address
	int var = -1;	   // for VAR: lvar index
	bool ref = false;  // is it a &x
};

struct call_t {
	ea_t ea = BADADDR;	   // call site
	ea_t callee = BADADDR; // BADADDR for indirect calls
	std::vector<call_arg_t> args;
};

inline call_arg_t read_call_arg(cexpr_t* a) {

	call_arg_t r;

	a = strip_casts(a);
	if (a->op == cot_ref) {
		r.ref = true;
		a = strip_casts(a->x);
	}

	switch (a->op) {

	case cot_num:
		r.k = call_arg_t::NUM;
		r.num = a->numval();
		break;

	case cot_obj:
		r.k = call_arg_t::OBJ;
		r.ea = a->obj_ea;
		break;

	case cot_var:
		r.k = call_arg_t::VAR;
		r.var = a->v.idx;
		break;

	case cot_str:
		// text only, no address
		r.k = call_arg_t::STR;
		break;

	default: break;
	}

	return r;
}

// Every call in a subtree, in visit order
struct call_collector_t : public ctree_visitor_t {
	std::vector<call_t> calls;

	call_collector_t() : ctree_visitor_t(CV_FAST) {}

	int idaapi visit_expr(cexpr_t* e) override {
		if (e->op != cot_call || e->a == nullptr) return 0;

		call_t c;
		c.ea = e->ea;
		cexpr_t* fn = strip_casts(e->x);
		if (fn->op == cot_obj) c.callee = fn->obj_ea;

		for (size_t i = 0; i < e->a->size(); i++)
			c.args.push_back(read_call_arg(&e->a->at(i)));

		calls.push_back(std::move(c));

		return 0; // calls nested in arguments
	}
};

} // namespace rv