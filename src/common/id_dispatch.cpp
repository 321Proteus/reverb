#include "id_dispatch.hpp"
#include "analysis.hpp"
#include "stores.hpp"

namespace rv {

// The comparison call in an if condition, and the branch that holds the match
static cexpr_t* cmp_call(cexpr_t* c, bool& in_then) {
	c = strip_casts(c);
	in_then = false;

	if (c->op == cot_lnot) {
		in_then = true;
		c = strip_casts(c->x);
	} else if (c->op == cot_eq || c->op == cot_ne) {
		cexpr_t* z = strip_casts(c->y);
		if (z->op != cot_num || z->numval() != 0) return nullptr;
		in_then = c->op == cot_eq;
		c = strip_casts(c->x);
	}
	return c->op == cot_call ? c : nullptr;
}

static bool literal_arg(cexpr_t* call, qstring& out) {
	for (carg_t& a : *call->a) {
		cexpr_t* e = strip_casts(&a);
		if (e->op == cot_ref) e = strip_casts(e->x);

		if (e->op == cot_str && e->string != nullptr) {
			out = e->string;
			return true;
		}
		if (e->op == cot_obj && !is_code_addr(e->obj_ea) &&
			read_utf8(e->obj_ea, out) && !out.empty())
			return true;
	}
	return false;
}

// return &x; or return x; (array decay)
static ea_t returned_obj(cinsn_t* i) {
	if (i != nullptr && i->op == cit_block)
		i = i->cblock->empty() ? nullptr : &i->cblock->front();
	if (i == nullptr || i->op != cit_return) return BADADDR;

	cexpr_t* e = strip_casts(&i->creturn->expr);
	if (e->op == cot_ref) e = strip_casts(e->x);
	return e->op == cot_obj ? e->obj_ea : BADADDR;
}

struct id_return_finder_t : public ctree_visitor_t {
	std::set<int> vars;
	std::vector<id_return_t> out;

	id_return_finder_t(int var) : ctree_visitor_t(CV_FAST) { vars.insert(var); }

	int idaapi visit_insn(cinsn_t* i) override {
		if (i->op != cit_if) return 0;

		bool in_then;
		cexpr_t* call = cmp_call(&i->cif->expr, in_then);
		id_return_t r;
		if (call == nullptr || arg_pos(call, vars) < 0 ||
			!literal_arg(call, r.id))
			return 0;

		r.ea = returned_obj(in_then ? i->cif->ithen : i->cif->ielse);
		if (r.ea != BADADDR) out.push_back(r);
		return 0;
	}
};

std::vector<id_return_t> find_id_returns(ea_t fn_ea, int arg) {
	cfuncptr_t cf = decompile_at(fn_ea);
	if (cf == nullptr || arg >= int(cf->argidx.size())) return {};

	id_return_finder_t f(cf->argidx[arg]);
	f.apply_to(&cf->body, nullptr);
	return f.out;
}

} // namespace rv
