#include "decomp/stores.hpp"
#include "decomp/sse.hpp"
#include "decomp/analysis.hpp"
#include <pro.h>

int64 scaled_off(const cexpr_t* e, int64 k) {
	tinfo_t t = e->type;
	if (!t.is_ptr() && e->x != nullptr) t = e->x->type;
	if (t.is_ptr()) {
		const int es = t.get_ptrarr_objsize();
		if (es > 1) k *= es;
	}
	return k;
}

bool fold_addr(cexpr_t* e, const alias_map_t* alias, loc_t& out) {
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

	default: return false;
	}
}

bool fold_idx(cexpr_t* e, const alias_map_t* alias, loc_t& out) {
	cexpr_t* n = strip_casts(e->y);
	if (n->op != cot_num) return false;

	// A local array decays to its own address, like a pointer
	int sz;
	const bool member_array =
		e->x->type.is_array() && strip_casts(e->x)->op != cot_var;
	const bool ok = member_array ? fold_lvalue(e->x, alias, out, sz)
								 : fold_addr(e->x, alias, out);
	if (!ok) return false;

	const int es = e->x->type.get_ptrarr_objsize();
	out.off += int64(n->numval()) * (es > 0 ? es : 1);
	return true;
}

bool fold_lvalue(cexpr_t* e, const alias_map_t* alias, loc_t& out, int& size) {
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

bool is_load_of(cexpr_t* e, int var, int64 off) {
	loc_t l;
	int sz;
	return fold_lvalue(e, nullptr, l, sz) && l.var == var && l.off == off;
}

int arg_pos(const cexpr_t* call, const std::set<int>& vars) {
	if (call->op != cot_call || call->a == nullptr) return -1;

	for (size_t k = 0; k < call->a->size(); k++) {
		cexpr_t* a = strip_casts(&call->a->at(k));
		if (a->op == cot_var && vars.count(a->v.idx) != 0) return int(k);
	}

	return -1;
}

bool absorb_copy(cexpr_t* asg, std::set<int>& vars) {
	if (asg->op != cot_asg) return false;

	cexpr_t* l = strip_casts(asg->x);
	cexpr_t* r = strip_casts(asg->y);

	if (l->op != cot_var || r->op != cot_var || vars.count(r->v.idx) == 0)
		return false;
	vars.insert(l->v.idx);

	return true;
}

bool obj_is_array(const cexpr_t* e) {
	if (e->type.is_array()) return true;

	tinfo_t gt;
	return get_tinfo(&gt, e->obj_ea) && gt.is_array();
}

bool addr_taken(ea_t obj, const cfunc_t* cf) {
	func_t* pfn = get_func(cf->entry_ea);
	xrefblk_t xb;
	for (bool ok = xb.first_to(obj, XREF_DATA); ok; ok = xb.next_to())
		if (xb.type == dr_O && func_contains(pfn, xb.from)) return true;
	return false;
}

bool load_ptr_const(ea_t src, ea_t& out) {
	if (!is_off0(get_flags(src))) return false; // not an offset item

	if (get_item_size(src) < asize_t(ptr_size())) return false;

	ea_t t = read_ptr(src);
	if (!is_mapped(t)) return false; // unresolved fixup / external

	out = t;
	return true;
}

bool const_load(cexpr_t* e, ea_t& src) {
	e = strip_casts(e);

	if (e->op == cot_obj && !is_code_addr(e->obj_ea)) {
		src = e->obj_ea;
		return true;
	}

	// _mm_load_si128(&xmmword_X)
	return sse_load_src(e, false, src) && !is_code_addr(src);
}

bool read_const(ea_t src, int size, val_t& v) {

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

uint64 slice_num(const store_t& s, int64 rel, int fsz) {
	uint64 x = 0;
	if (s.val.raw_len > 0) {
		// Constant read from memory: slice the raw bytes (works for 16-byte
		// stores)
		for (int i = 0; i < fsz && rel + i < s.val.raw_len; i++)
			x |= uint64(s.val.raw[rel + i]) << (8 * i);
	} else {
		// Immediate: at most 8 bytes in num; anything beyond is zero
		// (e.g. *(_OWORD *)p = 0)
		const int64 shift = rel * 8;
		x = (shift < 64) ? (s.val.num >> shift) : 0;
		if (fsz < 8) x &= (1ULL << (fsz * 8)) - 1;
	}
	return x;
}

bool stored_const(const std::vector<store_t>& stores, int var, int64 off,
	int size, uint64& out) {
	bool found = false;
	for (const store_t& s : stores) {
		if (s.loc.var != var || s.is_or || off < s.loc.off ||
			off + size > s.loc.off + s.size)
			continue;

		found = true;
		if (s.val.t == val_t::NUM) out = slice_num(s, off - s.loc.off, size);
		else if (s.val.t == val_t::ADDR && s.loc.off == off && s.size == size)
			out = s.val.ea;
		else
			found = false; // runtime value
	}
	return found;
}

bool inline_string(const std::vector<store_t>& stores, int var, qstring& out) {
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

bool is_frame_object(const cfunc_t* cf, int var) {
	const lvars_t& lv = cf->mba->vars;
	if (var < 0 || size_t(var) >= lv.size()) return false;

	const lvar_t& v = lv[var];
	return v.is_stk_var() && !v.type().is_ptr();
}

bool frame_loc(const cfunc_t* cf, loc_t& l) {
	if (!is_frame_object(cf, l.var)) return false;

	l = loc_t{ FRAME_ROOT, int64(cf->mba->vars[l.var].get_stkoff()) + l.off };
	return true;
}

std::vector<store_t> frame_view(
	const cfunc_t* cf, const std::vector<store_t>& stores) {
	std::vector<store_t> out;
	for (store_t s : stores)
		if (frame_loc(cf, s.loc)) out.push_back(std::move(s));
	return out;
}

val_t store_collector_t::value_of(cexpr_t* e) {
	e = strip_casts(e);
	val_t v;
	if (!e) return v;

	if (e->op == cot_var) {
		auto it = var_vals.find(e->v.idx);
		if (it != var_vals.end()) return it->second;
	}

	switch (e->op) {

	case cot_str:
		if (e->string != nullptr) {
			v.t = val_t::STR;
			v.str = e->string;
		}
		return v;

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
		if ((e->type.is_ptr() || e->type.get_size() == size_t(ptr_size())) &&
			fold_addr(e, &alias, l)) {
			v.t = val_t::LOCAL;
			v.loc = l;
		}
		return v;
	}
	}
}

int idaapi store_collector_t::visit_expr(cexpr_t* e) {
	if (e->op == cot_call) {
		collect_sse_store(e);
		add_call_copy(e);
		if (inline_calls) inline_callee(e);
		return 0;
	}

	if (e->op == cot_sub) {
		add_sub_copy(e);
		return 0;
	}

	if (e->op != cot_asg && e->op != cot_asgbor) return 0;

	// Alias tracking vX = (addr expr)
	if (e->op == cot_asg) {
		cexpr_t* lhs = strip_casts(e->x);
		if (lhs->op == cot_var) {
			const int var = lhs->v.idx;
			loc_t l;
			if (fold_addr(e->y, &alias, l) && l.var != var)
				alias[var] = l; // e.g. v3 = a1 + 48
			else
				alias.erase(var); // reassigned to something else

			// val assignment, later copied from
			const val_t held = value_of(e->y);
			if (held.t == val_t::ADDR || held.t == val_t::STR)
				var_vals[var] = held;
			else
				var_vals.erase(var);

			// c = *src++
			if (int src; post_inc_deref(e->y, src)) byte_src[var] = src;
			else
				byte_src.erase(var);

			// e.g. v14 = _mm_loadl_epi64(&off_X)
			val_t low;
			if (lhs->type.get_size() == 16 &&
				(low = low_qword(e->y)).t != val_t::UNKNOWN)
				sse_low[var] = low;
			else
				sse_low.erase(var);

			// v29 = 16 may be a member of a split stack struct
			const size_t sz = lhs->type.get_size();
			if (sz != BADSIZE && sz > 0 && sz <= 8 &&
				is_frame_object(cf, var)) {

				store_t s;
				s.loc = loc_t{ var, 0 };
				s.size = int(sz);
				s.ea = e->ea;
				s.text = expr_str(e, cf);

				add_store(std::move(s), e->y);
			}

			return 0;
		}
	}

	if (e->op == cot_asg && add_loop_copy(e)) return 0;

	store_t s;
	if (!fold_lvalue(e->x, &alias, s.loc, s.size)) return 0;

	s.is_or = (e->op == cot_asgbor);
	s.ea = e->ea;
	s.text = expr_str(e, cf);
	add_store(std::move(s), e->y);
	return 0;
}

bool store_collector_t::post_inc_deref(cexpr_t* e, int& var) {
	e = strip_casts(e);
	if (e == nullptr || e->op != cot_ptr) return false;

	cexpr_t* inc = strip_casts(e->x);
	if (inc->op != cot_postinc) return false;

	cexpr_t* v = strip_casts(inc->x);
	if (v->op != cot_var) return false;

	var = v->v.idx;
	return true;
}

bool store_collector_t::addr_loc(cexpr_t* e, loc_t& out) {
	cexpr_t* d = strip_casts(e);
	if (d->op == cot_ref) d = strip_casts(d->x);

	if (d->op == cot_obj) {
		out = loc_t{ GLOBAL_ROOT, int64(d->obj_ea) };
		return true;
	}
	return fold_addr(e, &alias, out) || fold_addr(d, &alias, out);
}

val_t store_collector_t::copy_src(cexpr_t* e) {
	cexpr_t* s = strip_casts(e);
	if (s->op == cot_ref) s = strip_casts(s->x);

	val_t v;
	if (s->op == cot_obj) {
		v.t = val_t::ADDR;
		v.ea = s->obj_ea;
		return v;
	}

	v = value_of(s);
	return v.t == val_t::ADDR || v.t == val_t::STR ? v : val_t{};
}

void store_collector_t::add_call_copy(cexpr_t* call) {
	cexpr_t* fn = strip_casts(call->x);
	if (fn->op != cot_helper || call->a == nullptr || call->a->size() < 2)
		return;

	bool copy = false;
	std::vector<qstring> funcs = { "strcpy", "strncpy", "memcpy", "qmemcpy",
		"strcpy_s" };
	for (const qstring& func : funcs) {
		copy = copy || strcmp(fn->helper, func.c_str()) == 0;
	}

	if (!copy) return;

	store_t c;
	if (!addr_loc(&call->a->at(0), c.loc)) return;

	c.val = copy_src(&call->a->at(1));
	if (c.val.t == val_t::UNKNOWN) return;

	c.ea = call->ea;
	c.text = expr_str(call, cf);
	copies.push_back(std::move(c));
}

void store_collector_t::add_sub_copy(cexpr_t* e) {
	store_t c;
	if (!addr_loc(e->y, c.loc)) return;

	// The source can come first transformed: (p + "abc") - base, with p the
	// destination pointer. UNKNOWN for a string built at runtime
	cexpr_t* src = strip_casts(e->x);
	if (src->op == cot_add) {
		cexpr_t* y = strip_casts(src->y);
		src = y->op == cot_obj || y->op == cot_ref ? y : src->x;
	}
	c.val = copy_src(src);

	c.ea = e->ea;
	c.text = expr_str(e, cf);
	copies.push_back(std::move(c));
}

bool store_collector_t::add_loop_copy(cexpr_t* asg) {
	int dst, src;
	if (!post_inc_deref(asg->x, dst)) return false;

	cexpr_t* y = strip_casts(asg->y);
	if (y->op == cot_var) {
		auto it = byte_src.find(y->v.idx);
		if (it == byte_src.end()) return false;
		src = it->second;
	} else if (!post_inc_deref(y, src)) {
		return false;
	}

	store_t c;
	auto a = alias.find(dst);
	c.loc = a != alias.end() ? a->second : loc_t{ dst, 0 };

	auto it = var_vals.find(src);
	if (it != var_vals.end()) c.val = it->second;

	c.ea = asg->ea;
	c.text = expr_str(asg, cf);
	copies.push_back(std::move(c));
	return true;
}

void store_collector_t::inline_callee(cexpr_t* call) {
	if (call->a == nullptr || call->a->empty()) return;

	cexpr_t* fn = strip_casts(call->x);
	if (fn->op != cot_obj || !is_code_addr(fn->obj_ea)) return;

	loc_t base;
	if (!addr_loc(&call->a->at(0), base)) return;

	const ea_t callee = rv::resolve_thunk(fn->obj_ea);
	if (in_extern_seg(callee)) return;

	cfuncptr_t ccf = rv::decompile_at(callee);
	if (ccf == nullptr || ccf->argidx.empty()) return;

	store_collector_t sub(&*ccf);
	const size_t n = std::min(call->a->size(), ccf->argidx.size());
	for (size_t i = 1; i < n; i++) {
		const val_t v = value_of(&call->a->at(i));
		if (v.t == val_t::NUM || v.t == val_t::ADDR || v.t == val_t::STR)
			sub.var_vals[ccf->argidx[i]] = v;
	}
	sub.apply_to(&ccf->body, nullptr);

	// The callee's this, moved to base; other callee locals mean nothing here
	const int self = ccf->argidx[0];
	auto move = [&](store_t s, std::vector<store_t>& to) {
		if (s.loc.var != self) return false;
		s.loc = loc_t{ base.var, base.off + s.loc.off };

		if (s.val.t == val_t::LOCAL) {
			if (s.val.loc.var == self)
				s.val.loc = loc_t{ base.var, base.off + s.val.loc.off };
			else
				s.val = val_t{};
		}
		to.push_back(std::move(s));
		return true;
	};

	unsigned ns = 0, nc = 0;
	for (const store_t& s : sub.stores)
		ns += move(s, stores);
	for (const store_t& c : sub.copies)
		nc += move(c, copies);

	if (ns + nc != 0)
		rv_log("  inlined %a called at %a: %u stores, %u string copies", callee,
			call->ea, ns, nc);
}
