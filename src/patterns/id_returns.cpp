#include "id_returns.hpp"
#include "core/log.hpp"
#include "db/segments.hpp"
#include "db/read.hpp"
#include "decomp/ctree.hpp"
#include "decomp/analysis.hpp"
#include "decomp/stores.hpp"
#include <map>

namespace rv {

/**
 * @brief The compared value in an if condition (a call, or the result variable
 * of an inlined compare loop)
 *
 * Accepts `cmp`, `!cmp` and `cmp ==`/`!= 0`.
 *
 * @param c       Condition
 * @param in_then Receives whether the match is in the then-branch
 * @return The call or variable, or null
 */
static cexpr_t* cmp_operand(cexpr_t* c, bool& in_then) {
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
	return c->op == cot_call || c->op == cot_var ? c : nullptr;
}

/**
 * @brief Whether @p e is a string literal, or the address of one in data; @p
 * out receives its text
 */
static bool literal_expr(cexpr_t* e, qstring& out) {
	e = strip_casts(e);
	if (e->op == cot_ref) e = strip_casts(e->x);

	if (e->op == cot_str && e->string != nullptr) {
		out = e->string;
		return true;
	}
	return e->op == cot_obj && !is_code_addr(e->obj_ea) &&
		   read_utf8(e->obj_ea, out) && !out.empty();
}

/**
 * @brief Whether one of the arguments of @p call is a string literal; @p out
 * receives it
 */
static bool literal_arg(cexpr_t* call, qstring& out) {
	for (carg_t& a : *call->a)
		if (literal_expr(&a, out)) return true;
	return false;
}

/**
 * @brief The statement itself, or the last one of a block
 *
 * A branch that returns ends in the return, even when it logs something first.
 */
static cinsn_t* last_stmt(cinsn_t* i) {
	if (i != nullptr && i->op == cit_block)
		i = i->cblock->empty() ? nullptr : &i->cblock->back();
	return i;
}

/**
 * @brief The object that `return &x;` or `return x;` (array decay) returns, or
 * BADADDR
 */
static ea_t returned_obj(cinsn_t* i) {
	i = last_stmt(i);
	if (i == nullptr || i->op != cit_return) return BADADDR;

	cexpr_t* e = strip_casts(&i->creturn->expr);
	if (e->op == cot_ref) e = strip_casts(e->x);
	return e->op == cot_obj ? e->obj_ea : BADADDR;
}

/**
 * @brief Whether @p i returns `0` or `nullptr`
 */
static bool returns_null(cinsn_t* i) {
	i = last_stmt(i);
	if (i == nullptr || i->op != cit_return) return false;

	cexpr_t* e = strip_casts(&i->creturn->expr);
	return e->op == cot_num && e->numval() == 0;
}

/**
 * @brief Whether @p c is an `&&` or `||`
 */
static bool is_compound(const cexpr_t* c) {
	return c->op == cot_land || c->op == cot_lor;
}

/**
 * @brief Finds the statement carrying the goto label @c num
 */
struct label_finder_t : public ctree_visitor_t {
	int num;
	cinsn_t* found = nullptr;

	label_finder_t(int n) : ctree_visitor_t(CV_FAST), num(n) {}

	int idaapi visit_insn(cinsn_t* i) override {
		if (i->label_num != num) return 0;
		found = i;
		return 1;
	}
};

/**
 * @brief Maps the result variables of inlined compare loops to the literal they
 * compare with
 */
struct inline_cmp_finder_t : public ctree_visitor_t {
	const std::set<int>& vars;
	std::map<int, qstring> ids;

	inline_cmp_finder_t(const std::set<int>& v)
		: ctree_visitor_t(CV_FAST), vars(v) {}

	struct loop_t : public ctree_visitor_t {
		const std::set<int>& vars;
		qstring lit;
		std::set<int> assigned;

		loop_t(const std::set<int>& v) : ctree_visitor_t(CV_FAST), vars(v) {}

		int idaapi visit_expr(cexpr_t* e) override {
			if (e->op == cot_sub) {
				cexpr_t* y = strip_casts(e->y);
				if (y->op == cot_var && vars.count(y->v.idx) != 0)
					literal_expr(e->x, lit);
			} else if (e->op == cot_asg) {
				cexpr_t* l = strip_casts(e->x);
				if (l->op == cot_var) assigned.insert(l->v.idx);
			}
			return 0;
		}
	};

	int idaapi visit_insn(cinsn_t* i) override {
		if (i->op != cit_do && i->op != cit_while && i->op != cit_for) return 0;

		loop_t l(vars);
		l.apply_to(i, nullptr);
		if (l.lit.empty()) return 0;
		for (int v : l.assigned)
			ids[v] = l.lit;
		return 0;
	}
};

/**
 * @brief Finds the `if (cmp(id, "literal")) return obj;` patterns of a function
 */
struct id_return_finder_t : public ctree_visitor_t {
	cfunc_t* cf;
	std::set<int> vars;
	std::map<int, qstring>
		inline_ids; /**< Result variable of an inlined compare -> literal */
	std::vector<id_return_t> out;

	id_return_finder_t(cfunc_t* f, int var) : ctree_visitor_t(CV_FAST), cf(f) {
		vars.insert(var);
		inline_cmp_finder_t icf(vars);
		icf.apply_to(&cf->body, nullptr);
		inline_ids = std::move(icf.ids);
	}

	/**
	 * @brief Whether @p c is `cmp(id, "literal")` in any of the accepted forms
	 *
	 * @param c       Condition
	 * @param in_then Receives whether the match is in the then-branch
	 * @param id      Receives the literal
	 */
	bool id_cmp(cexpr_t* c, bool& in_then, qstring& id) {
		cexpr_t* x = cmp_operand(c, in_then);
		if (x == nullptr) return false;
		if (x->op == cot_var) {
			auto it = inline_ids.find(x->v.idx);
			if (it == inline_ids.end()) return false;
			id = it->second;
			return true;
		}
		return arg_pos(x, vars) >= 0 && literal_arg(x, id);
	}

	/**
	 * @brief The object a matched branch returns
	 *
	 * A goto is followed to its label, and a branch ending in `if (other)
	 * return x;` makes the match conditional.
	 *
	 * @param i    Branch
	 * @param cond Set when the match is conditional
	 * @return The returned object, or BADADDR
	 */
	ea_t branch_obj(cinsn_t* i, bool& cond, int depth = 0) {
		i = last_stmt(i);
		if (i == nullptr || depth > 4) return BADADDR;

		if (i->op == cit_goto) {
			label_finder_t lf(i->cgoto->label_num);
			lf.apply_to(&cf->body, nullptr);
			return branch_obj(lf.found, cond, depth + 1);
		}
		if (i->op == cit_if && i->cif->ielse == nullptr) {
			cond = true;
			return branch_obj(i->cif->ithen, cond, depth + 1);
		}
		return returned_obj(i);
	}

	/**
	 * @brief Collects the ids of a compound condition that make it true, so
	 * they match in the then-branch
	 *
	 * @param cond Ids are conditional (under an `&&`)
	 * @return false if the condition cannot be read that way
	 */
	bool collect(cexpr_t* c, bool cond, std::vector<id_return_t>& ids) {
		c = strip_casts(c);
		if (is_compound(c)) {
			const bool land = c->op == cot_land;
			return collect(c->x, cond || land, ids) &&
				   collect(c->y, cond || land, ids);
		}

		bool in_then;
		id_return_t r;
		if (!id_cmp(c, in_then, r.id)) return true; // not about the id
		if (!in_then) return false;

		r.conditional = cond;
		ids.push_back(r);
		return true;
	}

	/**
	 * @brief The ids an if compares with, and whether they match in the
	 * then-branch
	 */
	bool id_check(cinsn_t* i, bool& in_then, std::vector<id_return_t>& ids) {
		ids.clear();
		if (i->op != cit_if) return false;

		cexpr_t* c = strip_casts(&i->cif->expr);
		if (is_compound(c)) {
			in_then = true;
			if (!collect(c, false, ids)) {
				rv_log("%a: negated compound id check, skipped", i->ea);
				ids.clear();
			}
			return !ids.empty();
		}

		// !(A && B)
		if (c->op == cot_lnot && is_compound(strip_casts(c->x))) {
			std::vector<id_return_t> tmp;
			if (!collect(c->x, false, tmp) || !tmp.empty())
				rv_log("%a: negated compound id check, skipped", i->ea);
			return false;
		}

		id_return_t r;
		if (!id_cmp(c, in_then, r.id)) return false;
		ids.push_back(r);
		return true;
	}

	/**
	 * @brief Records @p ids as returning what the branch returns, if it returns
	 * an object
	 */
	void add(std::vector<id_return_t>& ids, cinsn_t* branch) {
		bool cond = false;
		const ea_t ea = branch_obj(branch, cond);
		if (ea == BADADDR) return;
		for (id_return_t& r : ids) {
			r.ea = ea;
			r.conditional |= cond;
			out.push_back(r);
		}
	}

	int idaapi visit_insn(cinsn_t* i) override {
		std::vector<id_return_t> ids;
		bool in_then;
		if (id_check(i, in_then, ids)) {
			add(ids, in_then ? i->cif->ithen : i->cif->ielse);
			return 0;
		}
		if (i->op != cit_block) return 0;

		// if (cmp(id, "a")) return 0; return &a;
		cblock_t* b = i->cblock;
		for (auto it = b->begin(); it != b->end(); ++it) {
			auto next = it;
			if (++next == b->end()) break;

			if (id_check(&*it, in_then, ids) && !in_then &&
				it->cif->ielse == nullptr && returns_null(it->cif->ithen))
				add(ids, &*next);
		}
		return 0;
	}
};

std::vector<id_return_t> find_id_returns(ea_t fn_ea, int arg) {
	cfuncptr_t cf = decompile_at(fn_ea);
	if (cf == nullptr || arg >= int(cf->argidx.size())) return {};

	id_return_finder_t f(cf, cf->argidx[arg]);
	f.apply_to(&cf->body, nullptr);
	return f.out;
}

} // namespace rv
