#include "decomp/stores.hpp"

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
