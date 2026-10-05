#pragma once

#include "util.hpp"
#include <hexrays.hpp>

// A location consisting of a lvar index and offset
static struct loc_t {
	int var = -1;
	int64 off = 0;
};

// What was stored in a memory location
static struct val_t {
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

static bool obj_is_array(const cexpr_t* e) {
	if (e->type.is_array()) return true;
	tinfo_t gt;
	return get_tinfo(&gt, e->obj_ea) && gt.is_array();
}

static bool load_ptr_const(ea_t src, ea_t& out) {
	const int psz = inf_is_64bit() ? 8 : 4;
	if (!is_off0(get_flags(src))) return false; // not an offset item
	if (get_item_size(src) < asize_t(psz)) return false;

	ea_t t = (psz == 8) ? ea_t(get_qword(src)) : ea_t(get_dword(src));
	if (!is_mapped(t)) return false; // unresolved fixup / external
	out = t;
	return true;
}

static bool const_load(cexpr_t* e, ea_t& src) {
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

static bool read_const(ea_t src, int size, val_t& v) {

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
	std::map<int, loc_t> alias; // var -> (root var, delta)
	std::vector<store_t> stores;

	store_collector_t(const cfunc_t* f) : ctree_visitor_t(CV_FAST), cf(f) {}

	// Fold an address into a loc_t struct
	bool addr_of(cexpr_t* e, loc_t& out) {

		e = strip_casts(e);
		if (!e) return false;

		switch (e->op) {

		case cot_var: {
			auto it = alias.find(e->v.idx);
			out = (it != alias.end()) ? it->second : loc_t{ e->v.idx, 0 };
			return true;
		}

		case cot_add:
		case cot_sub: {

			cexpr_t* n = strip_casts(e->y);
			if (n->op != cot_num || !addr_of(e->x, out)) return false;

			int64 k = (int64)n->numval();

			// Pointer arithmetic e.g:
			// (_QWORD*)a1 + 3 = byte offset 24
			// (_DWORD*)a1 + 3 = byte offset 12
			tinfo_t t = e->type;
			if (!t.is_ptr()) t = e->x->type;
			if (t.is_ptr()) {
				size_t es = t.get_ptrarr_objsize();
				if (es != BADSIZE && es > 1) k *= int64(es);
			}
			out.off += (e->op == cot_add) ? k : -k;
			return true;
		}

		case cot_ref: { // &a1->field
			cexpr_t* x = strip_casts(e->x);

			if (x->op == cot_memptr && addr_of(x->x, out)) {
				out.off += x->m;
				return true;
			}

			return false;
		}

		default:
			return false;
		}
	}

	// Lvalue -> (loc, access_size)
	bool lvalue_of(cexpr_t* e, loc_t& out, int& size) {

		e = strip_casts(e);
		if (!e) return false;

		if (e->op == cot_ptr && addr_of(e->x, out)) {
			size = e->ptrsize;
			return true;
		}
		if (e->op == cot_memptr && addr_of(e->x, out)) {
			out.off += e->m;
			size = e->ptrsize;
			return true;
		}
		return false;
	}

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
			// A pointer into a tracked object
			loc_t l;
			if (addr_of(e, l)) {
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
				if (addr_of(e->y, l) && l.var != lhs->v.idx)
					alias[lhs->v.idx] = l; // e.g. v3 = a1 + 48
				else
					alias.erase(lhs->v.idx); // reassigned to something else
				return 0;
			}
		}

		store_t s;
		if (!lvalue_of(e->x, s.loc, s.size)) return 0;

		s.val = value_of(e->y);
		s.is_or = (e->op == cot_asgbor);
		s.ea = e->ea;
		s.text = expr_str(e, cf);

		// Wide constant copied out of data, e.g.
		//   *(__m128i *)(v2 + 92) = _mm_load_si128(&xmmword_XYZ)
		//   *(_OWORD *)(v2 + 28)  = xmmword_XYZ
		// value_of() reports these as UNKNOWN; read the bytes and treat the
		// store as a constant. Mach-O __const is marked writable, so const_load
		// only requires "not executable", not "read-only".
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