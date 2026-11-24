#pragma once

#include "core/log.hpp"
#include "db/segments.hpp"
#include "db/read.hpp"
#include "ctree.hpp"
#include "sse.hpp"
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
	enum val_type_t { UNKNOWN, NUM, ADDR, LOCAL, STR } t = UNKNOWN;
	uint64 num = 0;
	ea_t ea = BADADDR;
	loc_t loc;
	uchar raw[16] = {};
	int raw_len = 0;
	qstring str;
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

// Root of a global object. Its stores keep the absolute address in
// loc_t::off, so base_off + field offset still matches them
inline constexpr int GLOBAL_ROOT = -1;

/*
	Hex-Rays splits a struct built on the stack into one lvar per member.
	In frame terms they share FRAME_ROOT, at their stack offsets, so the
	members line up again
*/
inline constexpr int FRAME_ROOT = -2;

// var is stack storage of its own: not a pointer to the object
bool is_frame_object(const cfunc_t* cf, int var);

// loc in lvar terms -> loc in frame terms
bool frame_loc(const cfunc_t* cf, loc_t& l);

// The stores into frame objects, moved to FRAME_ROOT
std::vector<store_t> frame_view(
	const cfunc_t* cf, const std::vector<store_t>& stores);

struct store_collector_t : public ctree_visitor_t {
	const cfunc_t* cf;
	alias_map_t alias;
	std::map<int, val_t> sse_low;  // SSE local -> value of its low 8 bytes
	std::map<int, val_t> var_vals; // local -> address or literal it holds
	std::map<int, int> byte_src;   // c = *src++: c -> src
	std::vector<store_t> stores;

	// Add what direct callees store through their first argument
	bool inline_calls = false;

	// Levels of calls inline_calls follows (a constructor calling a member's)
	int inline_depth = 1;

	/*
		A callee parameter that gets a caller's local (a pointer passed on)
		stands for that local in the callee's stored values. Otherwise
		such values are UNKNOWN
	*/
	bool bind_locals = false;

	/*
		Strings copied into objects: strcpy calls, inlined strncpy and copy
		loops. Size 0, until a layout tells how wide the field is. val is
		the source, UNKNOWN for a string built at runtime
	*/
	std::vector<store_t> copies;

	store_collector_t(const cfunc_t* f) : ctree_visitor_t(CV_FAST), cf(f) {}

	val_t value_of(cexpr_t* e);

	/*
		The low 8 bytes of an SSE value holding a single scalar:
		_mm_loadl_epi64(&off_X) loads the pointer stored at off_X,
		(__m128i)(unsigned __int64)sub_X holds sub_X
	*/
	val_t low_qword(cexpr_t* e) {
		e = strip_casts(e);
		if (e == nullptr) return {};

		if (e->op == cot_var) {
			auto it = sse_low.find(e->v.idx);
			return it != sse_low.end() ? it->second : val_t{};
		}

		if (ea_t src; sse_load_src(e, true, src)) {
			val_t v;
			if (ea_t t; load_ptr_const(src, t)) {
				v.t = val_t::ADDR;
				v.ea = t;
			}
			return v;
		}

		if (e->type.get_size() > 8) return {}; // still a whole vector
		return value_of(e);
	}

	int idaapi visit_expr(cexpr_t* e) override;

	// _mm_storeu_si128((__m128i *)(a1 + 16), _mm_load_si128(&xmmword_X))
	void collect_sse_store(cexpr_t* e) {
		store_t s;
		cexpr_t *ptr, *value;
		if (!sse_store(e, ptr, value, s.size) ||
			!fold_addr(strip_casts(ptr), &alias, s.loc))
			return;

		s.ea = e->ea;
		s.text = expr_str(e, cf);
		add_store(std::move(s), value);
	}

	// *v++, the pointer local v
	static bool post_inc_deref(cexpr_t* e, int& var);

	// An address as a location: a global at GLOBAL_ROOT, or a folded one
	bool addr_loc(cexpr_t* e, loc_t& out);

	// Read source of a copy: a global, a literal or a local holding one
	val_t copy_src(cexpr_t* e);

	// strcpy(dst, src), memcpy(dst, src, n) etc
	void add_call_copy(cexpr_t* call);

	/*
		A constructor called rather than inlined, e.g. one the compiler
		specialized: PClassInfo2(&info, cid, ..., "Audio Module Class", ...).
		Its stores and copies through the first parameter land at the first
		argument, with the other parameters bound to the call's values
	*/
	void inline_callee(cexpr_t* call);

	// Inlined strncpy indexing the source from the destination
	void add_sub_copy(cexpr_t* e);

	// a *dst++ = *src++ loop
	bool add_loop_copy(cexpr_t* asg);

	/*
		Records s with the value of y. Shared by assignments and store
		intrinsics, s has everything but the value set
	*/
	void add_store(store_t s, cexpr_t* y) {
		/*
			Two 8-byte values written at once, e.g. two adjacent pointers:
			*(__m128i *)(p + 8) = _mm_unpacklo_epi64(v14, (__m128i)sub_X)
		*/
		cexpr_t *lo, *hi;
		if (!s.is_or && s.size == 16 && sse_unpacklo64(y, lo, hi)) {
			s.size = 8;
			for (cexpr_t* half : { lo, hi }) {
				store_t h = s;
				h.val = low_qword(half);
				stores.push_back(std::move(h));
				s.loc.off += 8;
			}
			return;
		}

		s.val = value_of(y);

		// The low part of an SSE value, _mm_storel_epi64(p, v14)
		if (s.val.t == val_t::UNKNOWN && !s.is_or && s.size <= 8)
			s.val = low_qword(y);

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
			if (const_load(y, src) && read_const(src, s.size, s.val))
				rv_log(
					"  const load: %d bytes from %a at %a", s.size, src, s.ea);
		}

		stores.push_back(std::move(s));
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