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

inline store_t num_store(loc_t at, int sz, uint64 v, ea_t ea) {
	store_t s;
	s.loc = at;
	s.size = sz;
	s.ea = ea;
	s.val.t = val_t::NUM;
	s.val.num = v;
	return s;
}

inline store_t copy_of(loc_t at, ea_t src, ea_t ea) {
	store_t c;
	c.loc = at;
	c.ea = ea;
	c.val.t = val_t::ADDR;
	c.val.ea = src;
	return c;
}

// var -> (root var, delta)
typedef std::map<int, loc_t> alias_map_t;

// Byte offset of p+k (e is the add/sub). Pointer arithmetic is scaled:
// (_QWORD *)a1 + 3 = byte offset 24, (_DWORD *)a1 + 3 = byte offset 12
int64 scaled_off(const cexpr_t* e, int64 k);

// Address of x[k]: x + k * sizeof(el)
bool fold_idx(cexpr_t* e, const alias_map_t* alias, loc_t& out);

// Fold an address expression into a loc_t
bool fold_addr(cexpr_t* e, const alias_map_t* alias, loc_t& out);

// Lvalue -> (loc, access_size)
bool fold_lvalue(cexpr_t* e, const alias_map_t* alias, loc_t& out, int& size);

// Is e a load of *(var + off)? Matches a1->field, *(_QWORD *)(a1 + 96) and
// *((_QWORD *)a1 + 12) alike.
bool is_load_of(cexpr_t* e, int var, int64 off);

// Position of the first call argument that is one of vars, or -1
int arg_pos(const cexpr_t* call, const std::set<int>& vars);

// vX = vY with vY in vars adds vX to vars
bool absorb_copy(cexpr_t* asg, std::set<int>& vars);

// Is the object an array?
bool obj_is_array(const cexpr_t* e);

/*
	Is the address taken in the function (lea, adrp + add) rather than its
	value loaded? Hex-Rays prints both as a bare object when it guesses a
	pointer type for an untyped item: lea rcx, off_X stores off_X
*/
bool addr_taken(ea_t obj, const cfunc_t* cf);

bool load_ptr_const(ea_t src, ea_t& out);

bool const_load(cexpr_t* e, ea_t& src);

// Load a complete multi-byte stored constant
bool read_const(ea_t src, int size, val_t& v);

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
			// Function, code, arr decay or an address taken with lea
			if (e->type.is_func() || is_code_addr(obj) || obj_is_array(e) ||
				addr_taken(obj, cf)) {
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

// Extract <= 8 bytes located rel bytes into a NUM store
uint64 slice_num(const store_t& s, int64 rel, int fsz);

/*
	The constant last stored over [off, off + size) of var: a number sliced
	out of a wider store, or an address stored exactly there. False when
	nothing constant was stored
*/
bool stored_const(const std::vector<store_t>& stores, int var, int64 off,
	int size, uint64& out);

// A string assembled from immediate stores into var, as left behind by an
// inlined strcpy: *(_QWORD *)a5 = "..."; *((_BYTE *)a5 + 5) = 0;
bool inline_string(const std::vector<store_t>& stores, int var, qstring& out);