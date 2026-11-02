#pragma once

#include "core/log.hpp"
#include "db/segments.hpp"
#include "db/read.hpp"
#include "ctree.hpp"
#include <algorithm>
#include <bytes.hpp>
#include <hexrays.hpp>
#include <map>
#include <set>
#include <vector>

// A location consisting of a lvar index and offset
struct loc_t {
	int var = -1;
	int64 off = 0;
};

// What was stored in a memory location
struct val_t {
	enum val_type_t { UNKNOWN, NUM, ADDR, LOCAL } t = UNKNOWN;
	uint64 num = 0;
	ea_t ea = BADADDR;
	loc_t loc;
	uchar raw[16] = {};
	int raw_len = 0;
};

// A single store
struct store_t {
	loc_t loc;
	int size = 0;
	val_t val;
	bool is_or = false;
	ea_t ea = BADADDR;
	qstring text;
};

// var -> (root var, delta)
typedef std::map<int, loc_t> alias_map_t;

// Byte offset of p+k (e is the add/sub). Pointer arithmetic is scaled:
// (_QWORD *)a1 + 3 = byte offset 24, (_DWORD *)a1 + 3 = byte offset 12
inline int64 scaled_off(const cexpr_t* e, int64 k) {
	tinfo_t t = e->type;
	if (!t.is_ptr() && e->x != nullptr) t = e->x->type;
	if (t.is_ptr()) {
		const int es = t.get_ptrarr_objsize();
		if (es > 1) k *= es;
	}
	return k;
}

inline bool fold_addr(cexpr_t* e, const alias_map_t* alias, loc_t& out);
inline bool fold_lvalue(
	cexpr_t* e, const alias_map_t* alias, loc_t& out, int& size);

// Address of x[k]: x + k * sizeof(el)
inline bool fold_idx(cexpr_t* e, const alias_map_t* alias, loc_t& out) {
	cexpr_t* n = strip_casts(e->y);
	if (n->op != cot_num) return false;

	int sz;
	const bool ok = e->x->type.is_array() ? fold_lvalue(e->x, alias, out, sz)
										  : fold_addr(e->x, alias, out);
	if (!ok) return false;

	const int es = e->x->type.get_ptrarr_objsize();
	out.off += int64(n->numval()) * (es > 0 ? es : 1);
	return true;
}

// Fold an address expression into a loc_t
inline bool fold_addr(cexpr_t* e, const alias_map_t* alias, loc_t& out) {
	e = strip_casts(e);
	if (e == nullptr) return false;

	switch (e->op) {

	case cot_var: {
		if (alias != nullptr) {
			auto it = alias->find(e->v.idx);
			if (it != alias->end()) {
				out = it->second;
				return true;
			}
		}
		out = loc_t{ e->v.idx, 0 };
		return true;
	}

	case cot_add:
	case cot_sub: {
		cexpr_t* n = strip_casts(e->y);
		if (n->op != cot_num || !fold_addr(e->x, alias, out)) return false;

		const int64 k = scaled_off(e, int64(n->numval()));
		out.off += (e->op == cot_add) ? k : -k;
		return true;
	}

	case cot_ref: { // &a1->field, &a1[3]
		int sz;
		return fold_lvalue(e->x, alias, out, sz);
	}

	default:
		return false;
	}
}

// Lvalue -> (loc, access_size)
inline bool fold_lvalue(
	cexpr_t* e, const alias_map_t* alias, loc_t& out, int& size) {
	e = strip_casts(e);
	if (e == nullptr) return false;

	if (e->op == cot_ptr && fold_addr(e->x, alias, out)) {
		size = e->ptrsize;
		return true;
	}
	if (e->op == cot_memptr && fold_addr(e->x, alias, out)) {
		out.off += e->m;
		size = e->ptrsize;
		return true;
	}
	if (e->op == cot_idx && fold_idx(e, alias, out)) { // a1[22]
		const size_t es = e->type.get_size();
		size = es == BADSIZE ? 0 : int(es);
		return true;
	}
	return false;
}

// Is e a load of *(var + off)? Matches a1->field, *(_QWORD *)(a1 + 96) and
// *((_QWORD *)a1 + 12) alike.
inline bool is_load_of(cexpr_t* e, int var, int64 off) {
	loc_t l;
	int sz;
	return fold_lvalue(e, nullptr, l, sz) && l.var == var && l.off == off;
}

// Position of the first call argument that is one of vars, or -1
inline int arg_pos(const cexpr_t* call, const std::set<int>& vars) {
	if (call->op != cot_call || call->a == nullptr) return -1;
	for (size_t k = 0; k < call->a->size(); k++) {
		cexpr_t* a = strip_casts(&call->a->at(k));
		if (a->op == cot_var && vars.count(a->v.idx) != 0) return int(k);
	}
	return -1;
}

// vX = vY with vY in vars adds vX to vars
inline bool absorb_copy(cexpr_t* asg, std::set<int>& vars) {
	if (asg->op != cot_asg) return false;
	cexpr_t* l = strip_casts(asg->x);
	cexpr_t* r = strip_casts(asg->y);
	if (l->op != cot_var || r->op != cot_var || vars.count(r->v.idx) == 0)
		return false;
	vars.insert(l->v.idx);
	return true;
}

inline bool obj_is_array(const cexpr_t* e) {
	if (e->type.is_array()) return true;
	tinfo_t gt;
	return get_tinfo(&gt, e->obj_ea) && gt.is_array();
}

inline bool load_ptr_const(ea_t src, ea_t& out) {
	if (!is_off0(get_flags(src))) return false; // not an offset item
	if (get_item_size(src) < asize_t(ptr_size())) return false;

	ea_t t = read_ptr(src);
	if (!is_mapped(t)) return false; // unresolved fixup / external
	out = t;
	return true;
}

inline bool const_load(cexpr_t* e, ea_t& src) {
	e = strip_casts(e);

	if (e->op == cot_obj && !is_code_addr(e->obj_ea)) {
		src = e->obj_ea;
		return true;
	}

	if (e->op == cot_call && e->a && e->a->size() == 1) {

		cexpr_t* fn = strip_casts(e->x);
		qstring name;
		if (fn->op == cot_helper) name = fn->helper;

		if (name == "_mm_load_si128" || name == "_mm_loadu_si128") {

			cexpr_t* a = strip_casts(&e->a->at(0));
			if (a->op == cot_ref) a = strip_casts(a->x);

			if (a->op == cot_obj && !is_code_addr(a->obj_ea)) {
				src = a->obj_ea;
				return true;
			}
		}
	}
	return false;
}

inline bool read_const(ea_t src, int size, val_t& v) {

	if (size <= 0 || size > int(sizeof(v.raw))) return false;
	if (get_bytes(v.raw, size, src) != size) // unloaded / partially loaded
		return false;

	v.t = val_t::NUM;
	v.raw_len = size;
	v.num = 0;

	for (int i = 0; i < size && i < 8; i++)
		v.num |= uint64(v.raw[i]) << (8 * i);

	return true;
}

struct store_collector_t : public ctree_visitor_t {
	const cfunc_t* cf;
	alias_map_t alias;
	std::vector<store_t> stores;

	store_collector_t(const cfunc_t* f) : ctree_visitor_t(CV_FAST), cf(f) {}

	val_t value_of(cexpr_t* e) {
		e = strip_casts(e);
		val_t v;
		if (!e) return v;

		switch (e->op) {

		case cot_num:
			v.t = val_t::NUM;
			v.num = e->numval();
			return v;

		case cot_obj: {
			const ea_t obj = e->obj_ea;
			// Function, code or arr decay
			if (e->type.is_func() || is_code_addr(obj) || obj_is_array(e)) {
				v.t = val_t::ADDR;
				v.ea = e->obj_ea;
			}

			// stored pytr load
			else if (ea_t t; load_ptr_const(obj, t)) {
				v.t = val_t::ADDR;
				v.ea = t;
			}

			// scalar handled by const_load
			return v;
		}

		case cot_ref: {
			// explicit address like &unk_X / &qword_X
			cexpr_t* x = strip_casts(e->x);
			if (x->op == cot_obj) {
				v.t = val_t::ADDR;
				v.ea = x->obj_ea;
			}
			return v;
		}

		default: {
			// A pointer into a tracked object. Pointer-sized only: a plain
			// int local (numPrograms = v30) also folds as a bare var.
			loc_t l;
			if ((e->type.is_ptr() ||
					e->type.get_size() == size_t(ptr_size())) &&
				fold_addr(e, &alias, l)) {
				v.t = val_t::LOCAL;
				v.loc = l;
			}
			return v;
		}
		}
	}

	int idaapi visit_expr(cexpr_t* e) override {
		if (e->op != cot_asg && e->op != cot_asgbor) return 0;

		// Alias tracking vX = (addr expr)
		if (e->op == cot_asg) {
			cexpr_t* lhs = strip_casts(e->x);
			if (lhs->op == cot_var) {
				loc_t l;
				if (fold_addr(e->y, &alias, l) && l.var != lhs->v.idx)
					alias[lhs->v.idx] = l; // e.g. v3 = a1 + 48
				else
					alias.erase(lhs->v.idx); // reassigned to something else
				return 0;
			}
		}

		store_t s;
		if (!fold_lvalue(e->x, &alias, s.loc, s.size)) return 0;

		s.val = value_of(e->y);
		s.is_or = (e->op == cot_asgbor);
		s.ea = e->ea;
		s.text = expr_str(e, cf);

		/*
			Wide constant copied out of data, e.g.
			*(__m128i *)(v2 + 92) = _mm_load_si128(&xmmword_XYZ)
			*(_OWORD *)(v2 + 28)  = xmmword_XYZ
			value_of() reports these as UNKNOWN; read the bytes and treat the
			store as a constant. Mach-O __const is marked writable, so
			const_load only requires "not executable", not "read-only".
		*/
		if (s.val.t == val_t::UNKNOWN && !s.is_or &&
			s.size <= int(sizeof(s.val.raw))) {
			ea_t src = BADADDR;
			if (const_load(e->y, src) && read_const(src, s.size, s.val)) {
				rv_log(
					"  const load: %d bytes from %a at %a", s.size, src, e->ea);
			}
		}

		stores.push_back(std::move(s));
		return 0;
	}
};

// String literals, constant returns, constants assigned to locals and calls
// in a subtree. Calls point into the cfunc and live as long as it does.
struct literal_collector_t : public ctree_visitor_t {
	std::vector<qstring> strs;
	std::vector<uint64> rets;
	std::vector<uint64> asgs; // result = 2400; break;
	std::vector<cexpr_t*> calls;

	literal_collector_t() : ctree_visitor_t(CV_FAST) {}

	int idaapi visit_expr(cexpr_t* e) override {
		if (e->op == cot_call) {
			calls.push_back(e);
			return 0;
		}

		if (e->op == cot_asg) {
			cexpr_t* l = strip_casts(e->x);
			cexpr_t* r = strip_casts(e->y);
			if (l->op == cot_var && r->op == cot_num)
				asgs.push_back(r->numval());
			return 0;
		}

		qstring s;
		if (e->op == cot_str && e->string != nullptr) s = e->string;
		else if (e->op != cot_obj || is_code_addr(e->obj_ea) ||
				 !read_cstr(e->obj_ea, s))
			return 0;

		if (std::find(strs.begin(), strs.end(), s) == strs.end())
			strs.push_back(s);
		return 0;
	}

	int idaapi visit_insn(cinsn_t* i) override {
		if (i->op != cit_return || i->creturn == nullptr) return 0;
		cexpr_t* r = strip_casts(&i->creturn->expr);
		if (r->op == cot_num) rets.push_back(r->numval());
		return 0;
	}
};

// A string assembled from immediate stores into var, as left behind by an
// inlined strcpy: *(_QWORD *)a5 = "..."; *((_BYTE *)a5 + 5) = 0;
inline bool inline_string(
	const std::vector<store_t>& stores, int var, qstring& out) {
	constexpr int MAX_LEN = 256;
	uchar buf[MAX_LEN] = {};
	bool have[MAX_LEN] = {};

	for (const store_t& s : stores) {
		if (s.loc.var != var || s.is_or || s.val.t != val_t::NUM) continue;
		for (int i = 0; i < s.size; i++) {
			const int64 at = s.loc.off + i;
			if (at < 0 || at >= MAX_LEN) continue;

			uchar b;
			if (s.val.raw_len > 0) b = i < s.val.raw_len ? s.val.raw[i] : 0;
			else {
				const int sh = inf_is_be() ? (s.size - 1 - i) : i;
				b = sh < 8 ? uchar(s.val.num >> (8 * sh)) : 0;
			}
			buf[at] = b;
			have[at] = true;
		}
	}

	out.clear();
	for (int i = 0; i < MAX_LEN && have[i] && buf[i] != 0; i++)
		out += char(buf[i]);
	return out.length() >= 2 && is_text(out);
}
