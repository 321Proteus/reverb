#include "hexrays.hpp"
#include "db/read.hpp"
#include "decomp/ctree.hpp"
#include <algorithm>

namespace rv::vtbl {

ea_t ref(cexpr_t* e) {
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
	return looks_like(vt) ? vt : BADADDR;
}

std::map<sval_t, ea_t> ctor_vtables(cfunc_t* cf, int var) {
	std::map<sval_t, ea_t> out;
	if (cf == nullptr || var < 0) return out;

	store_finder_t vf;
	vf.apply_to(&cf->body, nullptr);

	for (const auto& h : vf.hits)
		if (h.var_idx == var) out[h.this_offset] = h.vtable_ea;
	return out;
}

ea_t ctor_vtable(cfunc_t* cf, int this_var) {
	const auto v = ctor_vtables(cf, this_var);
	auto it = v.find(0);
	return it == v.end() ? BADADDR : it->second;
}

} // namespace rv::vtbl

namespace rv {

bool obj_tracker_t::vptr_of(cexpr_t* e, sval_t& off) const {
	e = strip_casts(e);
	if (e == nullptr) return false;

	switch (e->op) {
	case cot_var: {
		if (vtvars.count(e->v.idx) == 0) return false;
		auto it = var_vptr.find(e->v.idx);
		off = it == var_vptr.end() ? 0 : it->second;
		return true;
	}
	case cot_ptr: {
		if (is_object(e->x)) {
			off = 0;
			return true;
		}
		if (vptrs.empty()) return false;

		// *(obj + k)
		cexpr_t* a = strip_casts(e->x);
		if (a->op != cot_add) return false;
		cexpr_t* n = strip_casts(a->y);
		if (n->op != cot_num || !is_object(a->x)) return false;

		const sval_t k = sval_t(scaled_off(a, int64(n->numval())));
		if (vptrs.count(k) == 0) return false;
		off = k;
		return true;
	}
	case cot_memptr:
		if (!is_object(e->x)) return false;
		if (e->m != 0 && vptrs.count(sval_t(e->m)) == 0) return false;
		off = sval_t(e->m);
		return true;
	default:
		return false;
	}
}

bool obj_tracker_t::is_part(cexpr_t* e) const {
	e = strip_casts(e);
	if (e == nullptr) return false;

	// secondary vptrs belong to the object itself
	if (!vptrs.empty() && is_vtable(e)) return false;

	auto rooted = [&](cexpr_t* b) { return is_object(b) || is_part(b); };
	auto field = [&](cexpr_t* a) { // obj + k, k != 0
		a = strip_casts(a);
		if (a->op != cot_add) return false;
		cexpr_t* n = strip_casts(a->y);
		return n->op == cot_num && n->numval() != 0 && rooted(a->x);
	};

	switch (e->op) {
	case cot_var:
		return parts.count(e->v.idx) != 0;
	case cot_memptr:
		return e->m != 0 && rooted(e->x);
	case cot_ptr:
		return field(e->x);
	case cot_add:
		return field(e);
	case cot_ref: {
		cexpr_t* m = strip_casts(e->x);
		return m->op == cot_memptr && rooted(m->x);
	}
	default:
		return false;
	}
}

void obj_tracker_t::absorb(cexpr_t* asg) {
	if (asg->op != cot_asg) return;

	cexpr_t* l = strip_casts(asg->x);
	if (l->op != cot_var) return;
	const int v = l->v.idx;

	auto set_off = [&](sval_t off) {
		if (off != 0) var_vptr[v] = off;
		else var_vptr.erase(v);
	};

	int slot;
	sval_t off;
	if (is_object(asg->y)) objvars.insert(v);
	else if (vptr_of(asg->y, off)) {
		vtvars.insert(v);
		set_off(off);
	} else if (vcall_slot(asg->y, slot, &off)) {
		fnvars[v] = slot; // method hoisted out of a branch
		set_off(off);
	} else if (is_part(asg->y))
		parts.insert(v);
}

bool obj_tracker_t::vcall_slot(cexpr_t* callee, int& slot, sval_t* vptr_off) const {
	cexpr_t* e = strip_casts(callee);
	cexpr_t* vt = nullptr;
	int64 off = 0;

	switch (e->op) {

	case cot_var: {
		auto it = fnvars.find(e->v.idx);
		if (it == fnvars.end()) return false;
		slot = it->second;

		if (vptr_off) {
			auto o = var_vptr.find(e->v.idx);
			*vptr_off = o == var_vptr.end() ? 0 : o->second;
		}
		return true;
	}

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

	sval_t k;
	if (!vptr_of(vt, k)) return false;

	const int psz = ptr_size();
	if (off < 0 || off % psz != 0) return false;

	slot = int(off / psz);
	if (vptr_off) *vptr_off = k;
	return true;
}

} // namespace rv

namespace rv::vtbl {

void ctor_chain_leaves(
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
		std::find(out.begin(), out.end(), own) == out.end())
		out.push_back(own);
}

} // namespace rv::vtbl
