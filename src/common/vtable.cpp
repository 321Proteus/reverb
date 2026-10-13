#include "common/vtable.hpp"
#include <algorithm>

namespace rv {
int vtable_run_length(ea_t start_ea, int max_entries) {
	const int psz = ptr_size();
	int count = 0;

	for (int i = 0; i < max_entries; i++) {
		ea_t slot = start_ea + i * psz;
		if (!is_loaded(slot)) break;

		ea_t target = read_ptr(slot);
		if (target == 0 || !in_exec_seg(target)) break;
		count++;
	}
	return count;
}

ea_t vtable_ref(cexpr_t* e) {
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
	return looks_like_vtable(vt) ? vt : BADADDR;
}

ea_t ctor_vtable(cfunc_t* cf, int this_var) {
	if (cf == nullptr || this_var < 0) return BADADDR;

	vtable_store_finder_t vf;
	vf.apply_to(&cf->body, nullptr);

	ea_t last = BADADDR;
	for (const auto& h : vf.hits)
		if (h.this_offset == 0 && h.var_idx == this_var) last = h.vtable_ea;
	return last;
}

qstring rtti_class_name(ea_t vt) {
	const int psz = ptr_size();
	const ea_t meta = read_ptr(vt - psz);
	if (!is_loaded(meta)) return qstring();

	qstring raw;
	if (inf_get_filetype() == f_PE) {
		// COL signature 1 = relative offsets, 0 = pointers
		const uint32 sig = get_dword(meta);
		const ea_t td = sig == 1   ? get_imagebase() + get_dword(meta + 12)
						: sig == 0 ? ea_t(get_dword(meta + 12))
								   : BADADDR;

		if (td == BADADDR || !is_loaded(td)) return qstring();
		if (!read_cstr(td + 2 * psz, raw) || !raw.starts_with(".?A"))
			return qstring();

		// demangles, strip the suffix
		qstring dem;
		qstring mangled = qstring("??_R0") + raw.substr(1) + "@8";

		if (demangle_name(&dem, mangled.c_str(), MNG_SHORT_FORM) > 0) {
			size_t p = dem.find(" `RTTI");

			if (p != qstring::npos) dem.resize(p);
			if (dem.starts_with("class ")) dem.remove(0, 6);
			if (dem.starts_with("struct ")) dem.remove(0, 7);

			return dem;
		}

		return raw;
	}

	const ea_t name = read_ptr(meta + psz);

	if (!is_loaded(name) || !read_cstr(name, raw)) return qstring();
	if (raw.empty() || !(qisdigit(raw[0]) || raw[0] == 'N')) return qstring();

	qstring dem;
	qstring mangled = qstring("_Z") + raw;
	if (demangle_name(&dem, mangled.c_str(), MNG_SHORT_FORM) > 0) return dem;
	return raw;
}

tinfo_t find_class_type(const qstring& cls) {
	tinfo_t t;
	if (cls.empty()) return t;

	const char* names[] = { cls.c_str(), nullptr };
	const size_t p = cls.rfind(':');
	if (p != qstring::npos) names[1] = cls.c_str() + p + 1;

	for (const char* n : names) {
		if (n && t.get_named_type(nullptr, n) && t.is_struct() && t.get_size() != BADSIZE)
			return t;
	}
	return tinfo_t();
}

bool label_vtable(ea_t vtbl_ea, const char* type_name) {
	tinfo_t ti;
	if (!ti.get_named_type(nullptr, type_name)) return false;

	size_t sz = ti.get_size();
	if (sz == BADSIZE) return false;

	del_items(vtbl_ea, DELIT_EXPAND, sz);

	bool vtbl_tinfo = apply_tinfo(vtbl_ea, ti, TINFO_DEFINITE);
	if (!vtbl_tinfo) return false;

	udt_type_data_t udt;
	if (!ti.get_udt_details(&udt)) return false;

	const qstring class_name = rtti_class_name(vtbl_ea);
	const tinfo_t owner_ti = find_class_type(class_name);

	qstring owner = class_name;
	if (owner.empty()) {
		owner = type_name;
		if (owner.ends_with("_vtbl")) owner.resize(owner.length() - 5);
	}
	
	int i = 0;
	for (const udm_t& m : udt) {
		const ea_t slot = vtbl_ea + m.offset / 8;
		const ea_t target = read_ptr(slot);
		if (!target || !getseg(target)) continue;

		if (!get_func(target)) add_func(target);
		func_t* f = get_func(target);
		if (!f || f->start_ea != target) continue;

		qstring role;
		role.sprnt("%s::%s", owner.c_str(), m.name.c_str());
		
		int refs = 0;
		xrefblk_t xb;
		for (bool ok = xb.first_to(target, XREF_DATA); ok; ok = xb.next_to()) {
			if (xb.type != dr_O || is_code(get_flags(xb.from)))
				continue;

			const ea_t end = get_item_end(xb.from);
			int ptr = ptr_size();

			for (ea_t ref = xb.from; ref + ptr <= end; ref += ptr) {
				if (read_ptr(ref) == target) refs++;
			}
		}

		if (refs > 1) {
			qstring cmt;
			get_func_cmt(&cmt, f, true);

			if (cmt.find(role) == qstring::npos) {
				if (!cmt.empty()) cmt.append('\n');
				cmt.append(role);
				set_func_cmt(f, cmt.c_str(), true);
			}
			continue;
		}

		if (has_dummy_name(get_flags(target)) && (f->flags & FUNC_LIB) == 0)
			set_name(target, role.c_str(), SN_NOWARN | SN_FORCE);

		if (!is_userti(target)) {
			 // member is a pointer to function
			tinfo_t ft = m.type.get_pointed_object();
			if (!owner_ti.empty()) {
				func_type_data_t ftd;

				if (ft.get_func_details(&ftd) && !ftd.empty()) {
					tinfo_t this_ptr;
					this_ptr.create_ptr(owner_ti);
					ftd[0].type = this_ptr;
					ft.create_func(ftd);
				}
			}

			if (!ft.empty())
				apply_tinfo(target, ft, TINFO_DEFINITE);
		}

	}
	return true;
}

bool obj_tracker_t::is_part(cexpr_t* e) const {
	e = strip_casts(e);
	if (e == nullptr) return false;

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

	int slot;
	if (is_object(asg->y)) objvars.insert(l->v.idx);
	else if (is_vtable(asg->y))
		vtvars.insert(l->v.idx);
	else if (vcall_slot(asg->y, slot))
		fnvars[l->v.idx] = slot; // method hoisted out of a branch
	else if (is_part(asg->y))
		parts.insert(l->v.idx);
}

bool obj_tracker_t::vcall_slot(cexpr_t* callee, int& slot) const {
	cexpr_t* e = strip_casts(callee);
	cexpr_t* vt = nullptr;
	int64 off = 0;

	switch (e->op) {

	case cot_var: {
		auto it = fnvars.find(e->v.idx);
		if (it == fnvars.end()) return false;
		slot = it->second;
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

	if (!is_vtable(vt)) return false;

	const int psz = ptr_size();
	if (off < 0 || off % psz != 0) return false;

	slot = int(off / psz);
	return true;
}

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
		std::find(out.begin(), out.end(), own) == out.end()) {
		rv_log("Most derived ctor %a, vtable %a", ctor_ea, own);
		out.push_back(own);
	}
}

size_t count_vtable_slots(ea_t ea, size_t max) {

	const int ptr = ptr_size();
	size_t n = 0;

	for (; n < max; n++) {
		const ea_t slot = ea + n * ptr;
		if (n > 0 && has_xref(get_flags(slot))) break;

		const ea_t target = read_ptr(slot);
		const segment_t* s = getseg(target);
		if (s == nullptr || ((s->perm & SEGPERM_EXEC) == 0 && s->type != SEG_CODE))
			break;
	}
	return n;
}

const char* pick_vtable_type(ea_t ea, std::initializer_list<const char*> candidates) {

	const size_t ptr = ptr_size();
	size_t w = 0;
	for (const char* name : candidates) {
		tinfo_t ti;
		if (ti.get_named_type(nullptr, name) && ti.get_size() != BADSIZE)
			w = std::max(w, ti.get_size() / ptr);
	}

	const size_t h = count_vtable_slots(ea, w);

	for (const char* name : candidates) {
		tinfo_t ti;
		if (ti.get_named_type(nullptr, name) && ti.get_size() != BADSIZE &&
			ti.get_size() / ptr <= h)
			return name;
	}
	
	return nullptr;
}

} // namespace rv
