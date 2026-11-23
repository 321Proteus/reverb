#include "returns.hpp"
#include "analysis.hpp"
#include "core/log.hpp"
#include <map>
#include <set>
#include <vector>

namespace rv {

namespace {

/*
	Where the returned pointer points: (object var, this offset). Follows
	non-constant locals assigned once from an address, e.g.
		result = v2 + 24; return result;
	A returned local assigned more than once is not followed
	(it would read as itself at +0). A constant step of the returned local
	counts toward the offset, scaled by what result points to
*/
struct return_loc_finder_t : public ctree_visitor_t {
	std::map<int, int> asg_count;
	alias_map_t alias;
	std::set<int> reassigned;
	std::map<int, int64> bump; // local -> bytes it was stepped by
	std::vector<cexpr_t*> rets;

	return_loc_finder_t() : ctree_visitor_t(CV_FAST) {}

	// ++v, v -= 2...: true when e steps a local by a constant
	bool step(cexpr_t* e) {
		int64 n;
		switch (e->op) {
		case cot_preinc:
		case cot_postinc: n = 1; break;
		case cot_predec:
		case cot_postdec: n = -1; break;
		case cot_asgadd:
		case cot_asgsub:  {
			cexpr_t* k = strip_casts(e->y);
			if (k->op != cot_num) return false;
			n = int64(k->numval()) * (e->op == cot_asgadd ? 1 : -1);
			break;
		}
		default: return false;
		}

		cexpr_t* l = strip_casts(e->x);
		if (l->op != cot_var) return false;

		int64 unit = 1; // an integer local steps in bytes
		if (l->type.is_ptr()) {
			const size_t sz = l->type.get_ptrarr_objsize();
			if (sz == BADSIZE || sz == 0) {
				reassigned.insert(l->v.idx); // void *: unknown step
				return true;
			}
			unit = int64(sz);
		}

		bump[l->v.idx] += n * unit;
		return true;
	}

	int idaapi visit_expr(cexpr_t* e) override {
		if (step(e)) return 0;
		if (e->op != cot_asg) return 0;

		cexpr_t* l = strip_casts(e->x);
		if (l->op != cot_var) return 0;
		if (strip_casts(e->y)->op == cot_num) return 0; // result = nullptr

		const int v = l->v.idx;
		loc_t src;
		if (++asg_count[v] == 1 && fold_addr(e->y, &alias, src) && src.var != v)
			alias[v] = src;
		else
			alias.erase(v);

		if (asg_count[v] > 1) reassigned.insert(v);

		return 0;
	}

	int idaapi visit_insn(cinsn_t* i) override {
		if (i->op == cit_return && i->creturn != nullptr)
			rets.push_back(&i->creturn->expr);
		return 0;
	}

	bool get(loc_t& out) {
		bool have = false;

		for (cexpr_t* r : rets) {
			cexpr_t* s = strip_casts(r);
			if (s->op == cot_num) continue; // return nullptr

			loc_t l;
			if (!fold_addr(r, &alias, l) || reassigned.count(l.var) != 0)
				return false;

			// The step of the returned local itself. A stepped object the
			// alias points into leaves the order of the two unknown
			const int self = s->op == cot_var ? s->v.idx : -1;
			if (l.var != self && bump.count(l.var) != 0) return false;
			if (auto b = bump.find(self); b != bump.end()) l.off += b->second;

			if (have && (l.var != out.var || l.off != out.off)) return false;

			out = l;
			have = true;
		}

		return have;
	}
};

} // namespace

bool return_loc(cfunc_t* cf, loc_t& out) {
	return_loc_finder_t f;
	f.apply_to(&cf->body, nullptr);
	return f.get(out);
}

void type_return_base(ea_t fn, const char* label, int follow) {
	cfuncptr_t cf = decompile_at(fn, DECOMP_NO_WAIT | DECOMP_NO_CACHE);
	if (cf == nullptr) return;

	loc_t l;
	if (!return_loc(cf, l)) {
		const ea_t g = follow > 0 ? direct_tail_callee(fn) : BADADDR;
		if (g != BADADDR) {
			type_return_base(resolve_thunk(g), label, follow - 1);
			return;
		}
		rv_log("    %s: %a returns no object local plus offset", label, fn);
		return;
	}

	if (l.off == 0) return; // the object itself, nothing to adjust

	const lvars_t* lv = cf->get_lvars();
	if (l.var < 0 || size_t(l.var) >= lv->size()) return;

	const lvar_t& v = lv->at(l.var);
	if (v.has_user_type()) {
		rv_log("    %s: %a returns %s+0x%llX, left alone: %s has a user type",
			label, fn, v.name.c_str(), (uint64)l.off, v.name.c_str());
		return;
	}

	tinfo_t cp;
	cp.create_ptr(tinfo_t(BT_INT8 | BTMT_CHAR));
	if (set_lvar(fn, v, nullptr, &cp))
		rv_log("    %s: %a returns %s+0x%llX, %s typed char *", label, fn,
			v.name.c_str(), (uint64)l.off, v.name.c_str());
	else
		rv_log("    %s: %a returns %s+0x%llX, %s could not be retyped", label,
			fn, v.name.c_str(), (uint64)l.off, v.name.c_str());
}

} // namespace rv
