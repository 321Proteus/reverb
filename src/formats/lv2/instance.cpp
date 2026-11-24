#include "instance.hpp"
#include "uris.hpp"
#include "db/read.hpp"
#include "core/log.hpp"
#include "db/segments.hpp"
#include "decomp/analysis.hpp"
#include "decomp/returns.hpp"
#include "decomp/stores.hpp"
#include "ui/report.hpp"
#include <demangle.hpp>
#include <name.hpp>
#include <map>
#include <set>

namespace rv::lv2 {

static constexpr asize_t MAX_ALLOC_WRAPPER = 0x100; // bytes of code
// libstdc++: malloc, get_new_handler, the handler, bad_alloc allocate + throw
static constexpr int MAX_ALLOC_WRAPPER_CALLS = 6;

// Calls followed from instantiate for stores into the instance: the
// constructor, and the constructors of its members
static constexpr int MAX_CTOR_DEPTH = 3;

static bool named_any(ea_t ea, std::initializer_list<const char*> names) {
	qstring raw, dem;
	get_ea_name(&raw, ea);
	get_ea_name(&dem, ea, GN_SHORT | GN_DEMANGLED);
	for (const char* n : names) {
		if (raw == n || dem == n) return true;
		// imports and some abi symbols with a leading _
		if (raw.length() > 1 && raw[0] == '_' && raw.substr(1) == n)
			return true;
	}
	return false;
}

static bool is_malloc(ea_t ea) {
	return named_any(ea, { "malloc", "_malloc_base" });
}

/*
	operator new or malloc by name, or a small function that calls malloc
	and nothing much else (a statically linked operator new, which FLIRT
	does not always name)
*/
static bool is_allocator(ea_t ea) {
	if (is_malloc(ea) ||
		named_any(ea, { "operator new", "??2@YAPEAX_K@Z", "??2@YAPAXI@Z",
						  "_Znwm", "_Znwj", "_Znam", "_Znaj" }))
		return true;

	func_t* pfn = get_func(ea);
	if (pfn == nullptr || pfn->size() > MAX_ALLOC_WRAPPER) return false;

	cfuncptr_t cf = rv::decompile_at(ea);
	if (cf == nullptr) return false;

	literal_collector_t lc;
	lc.apply_to(&cf->body, nullptr);
	if (lc.calls.empty() || int(lc.calls.size()) > MAX_ALLOC_WRAPPER_CALLS)
		return false;

	for (cexpr_t* c : lc.calls) {
		cexpr_t* fn = strip_casts(c->x);
		if (fn->op == cot_obj && is_malloc(rv::resolve_thunk(fn->obj_ea))) {
			rv_log("%a calls malloc, taken for operator new", ea);
			return true;
		}
	}

	return false;
}

static ea_t direct_callee(cexpr_t* call) {
	cexpr_t* fn = strip_casts(call->x);
	return fn->op == cot_obj ? rv::resolve_thunk(fn->obj_ea) : BADADDR;
}

static bool is_var(cexpr_t* e, int var) {
	e = strip_casts(e);
	return e->op == cot_var && e->v.idx == var;
}

// Locals assigned an allocation of constant size, and the first call that
// passes each of them first and is a statement of its own (a constructor;
// a getter's result is used). var = other copies are followed one hop
struct alloc_finder_t : public ctree_parentee_t {
	struct rec_t {
		size_t size = 0;
		ea_t site = BADADDR, alloc = BADADDR, ctor = BADADDR;
	};

	int var;
	std::map<int, rec_t> recs;
	int src = -1; // the local var got its allocation from

	alloc_finder_t(int v) : var(v) {}

	int idaapi visit_expr(cexpr_t* e) override {
		if (e->op == cot_asg) {
			cexpr_t* l = strip_casts(e->x);
			cexpr_t* r = strip_casts(e->y);
			if (l->op != cot_var) return 0;

			// var = v25, after v25 = alloc(N)
			if (r->op == cot_var && l->v.idx == var && src < 0 &&
				recs.count(r->v.idx) != 0) {
				src = r->v.idx;
				return 0;
			}
			if (r->op != cot_call || r->a->empty()) return 0;

			cexpr_t* n = strip_casts(&r->a->at(0));
			const ea_t fn = direct_callee(r);
			if (n->op != cot_num || fn == BADADDR || !is_allocator(fn))
				return 0;

			rec_t& rec = recs[l->v.idx];
			if (rec.site == BADADDR) {
				rec.size = size_t(n->numval());
				rec.site = e->ea;
				rec.alloc = fn;
			}
			if (l->v.idx == var && src < 0) src = var;
			return 0;
		}

		if (e->op != cot_call || e->a->empty()) return 0;
		cexpr_t* a0 = strip_casts(&e->a->at(0));
		if (a0->op != cot_var) return 0;

		// After the allocation: var itself gets a record once it has one
		auto it = recs.find(a0->v.idx);
		if (it == recs.end() && a0->v.idx == var && src >= 0)
			it = recs.emplace(var, recs[src]).first;
		if (it == recs.end() || it->second.ctor != BADADDR) return 0;

		if (parents.empty() || parents.back() == nullptr ||
			parents.back()->is_expr())
			return 0;

		const ea_t fn = direct_callee(e);
		func_t* pfn = fn != BADADDR ? get_func(fn) : nullptr;
		if (pfn == nullptr || in_extern_seg(fn) || (pfn->flags & FUNC_LIB) != 0)
			return 0;

		it->second.ctor = fn;
		return 0;
	}

	// -> false when var got no allocation
	bool result(instance_t& out) const {
		if (src < 0) return false;
		const rec_t& r = recs.at(src);
		out.size = r.size;
		out.site = r.site;
		out.alloc = r.alloc;
		out.ctor = r.ctor;
		if (auto v = recs.find(var); out.ctor == BADADDR && v != recs.end())
			out.ctor = v->second.ctor;
		return true;
	}
};

// The one local every non-null return returns, however often it is assigned
struct returned_var_t : public ctree_visitor_t {
	int var = -1;
	bool mixed = false;

	returned_var_t() : ctree_visitor_t(CV_FAST) {}

	int idaapi visit_insn(cinsn_t* i) override {
		if (i->op != cit_return || i->creturn == nullptr) return 0;

		cexpr_t* e = strip_casts(&i->creturn->expr);
		if (e->op == cot_num) return 0; // return nullptr

		if (e->op != cot_var || (var >= 0 && e->v.idx != var)) mixed = true;
		else
			var = e->v.idx;

		return 0;
	}
};

bool find_instance(ea_t inst, instance_t& out) {
	out = {};
	out.inst = inst;

	cfuncptr_t cf = rv::decompile_at(inst);
	if (cf == nullptr) return false;

	loc_t l;
	if (rv::return_loc(cf, l) && l.off == 0 && l.var >= 0) {
		out.var = l.var;
	} else {
		// A local Hex-Rays merged with another value might be assigned more
		// than once
		returned_var_t rv;
		rv.apply_to(&cf->body, nullptr);
		if (rv.mixed || rv.var < 0) {
			rv_log("instantiate at %a returns no single local", inst);
			return false;
		}

		out.var = rv.var;
		out.shared_local = true;
		rv_log(
			"instantiate at %a returns a local assigned more than once", inst);
	}

	alloc_finder_t f(out.var);
	f.apply_to(&cf->body, nullptr);
	if (!f.result(out)) {
		rv_log("instantiate at %a: the returned object is not allocated "
			   "with a constant size",
			inst);
		return false;
	}

	rv_log("instantiate at %a allocates 0x%llX bytes at %a%s", inst,
		(uint64)out.size, out.site, out.ctor != BADADDR ? ", constructed" : "");

	if (out.ctor != BADADDR) rv_log("  constructor %a", out.ctor);
	return true;
}

// The local struct type_name, created fixed-size if missing
static bool instance_type(const char* type_name, size_t size, tinfo_t& out) {
	if (out.get_named_type(get_idati(), type_name)) {
		if (out.get_size() != size) {
			rv_log("%s exists with 0x%llX bytes, 0x%llX allocated; kept",
				type_name, (uint64)out.get_size(), (uint64)size);
		}

		return true;
	}

	udt_type_data_t udt;
	udt.taudt_bits |= TAUDT_FIXED; // members are added at their offsets
	udt.total_size = udt.unpadded_size = size;

	tinfo_t t;
	if (!t.create_udt(udt) ||
		t.set_named_type(get_idati(), type_name) != TERR_OK) {
		rv_log("Could not create %s", type_name);
		return false;
	}

	return out.get_named_type(get_idati(), type_name);
}

// A local typed by label_features(pointer to a feature's data type)
static bool feature_ptr(const tinfo_t& t) {
	qstring n;
	if (!t.is_ptr() || !t.get_pointed_object().get_type_name(&n)) return false;
	for (const uri_type_t& f : FEATURES)
		if (f.type != nullptr && n == f.type) return true;

	return false;
}

/*
	The feature pointers instantiate stores into the instance, itself or in
	the calls it passes both to (the constructor and the members' ones),
	added to the struct as members named and typed like the locals
*/
static void label_fields(ea_t inst, tinfo_t& st) {
	cfuncptr_t cf = rv::decompile_at(inst, DECOMP_NO_WAIT | DECOMP_NO_CACHE);
	loc_t l;
	if (cf == nullptr || !rv::return_loc(cf, l) || l.off != 0) return;

	store_collector_t col(cf);
	col.inline_calls = true;
	col.inline_depth = MAX_CTOR_DEPTH;
	col.bind_locals = true;
	col.apply_to(&cf->body, nullptr);

	const lvars_t* lv = cf->get_lvars();
	std::set<int64> done;

	for (const store_t& s : col.stores) {
		if (s.loc.var != l.var || s.val.t != val_t::LOCAL ||
			s.val.loc.off != 0 || s.size != ptr_size() ||
			!done.insert(s.loc.off).second)
			continue;

		const int v = s.val.loc.var;
		if (v < 0 || size_t(v) >= lv->size() || !feature_ptr(lv->at(v).type()))
			continue;

		udm_t cur;
		cur.offset = uint64(s.loc.off) * 8;
		if (st.find_udm(&cur, STRMEM_OFFSET) >= 0) continue; // kept

		udm_t m;
		m.name = lv->at(v).name;
		m.type = lv->at(v).type();
		m.offset = uint64(s.loc.off) * 8;
		m.size = uint64(ptr_size()) * 8;

		if (st.add_udm(m) != TERR_OK) { // the name is taken
			m.name.cat_sprnt("_%llX", (uint64)s.loc.off);
			if (st.add_udm(m) != TERR_OK) {
				rv_log("Could not add %s at +0x%llX", m.name.c_str(),
					(uint64)s.loc.off);
				continue;
			}
		}

		qstring t, row;
		m.type.print(&t);
		row.sprnt(
			"%s %s at +0x%llX", t.c_str(), m.name.c_str(), (uint64)s.loc.off);
		rv_log("Instance field %s, stored at %a", row.c_str(), s.ea);
		report::row("field", row, s.ea);
	}
}

void label_instance(const instance_t& in, const char* type_name) {
	tinfo_t st, ptr;
	if (!instance_type(type_name, in.size, st)) return;
	ptr.create_ptr(st);

	// Found again: applying instantiate's prototype may renumber its locals
	loc_t l;
	cfuncptr_t cf = rv::decompile_at(in.inst, DECOMP_NO_WAIT | DECOMP_NO_CACHE);
	if (cf != nullptr && rv::return_loc(cf, l) && l.off == 0) {
		const lvars_t* lv = cf->get_lvars();

		if (l.var >= 0 && size_t(l.var) < lv->size()) {
			const lvar_t& v = lv->at(l.var);
			if (!v.has_user_type() && !v.has_user_name())
				rv::set_lvar(in.inst, v, "instance", &ptr);
		}
	}

	mark_cfunc_dirty(in.inst);

	if (in.ctor != BADADDR) {
		if (!rv::set_arg_type(in.ctor, 0, ptr))
			rv_log("Could not type the constructor at %a", in.ctor);
		if (has_dummy_name(get_flags(in.ctor)))
			set_name(in.ctor, (qstring(type_name) + "_ctor").c_str(),
				SN_NOWARN | SN_FORCE);
		mark_cfunc_dirty(in.ctor);
	}

	qstring v;
	v.sprnt("%s (0x%llX bytes)", type_name, (uint64)in.size);
	report::row("instance", v, in.site);
	if (in.ctor != BADADDR) report::addr_row("constructor", in.ctor);

	label_fields(in.inst, st);
}

} // namespace rv::lv2
