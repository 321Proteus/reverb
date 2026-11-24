#include "instance.hpp"
#include "core/log.hpp"
#include "db/segments.hpp"
#include "decomp/analysis.hpp"
#include "decomp/returns.hpp"
#include "decomp/stores.hpp"
#include "ui/report.hpp"
#include <demangle.hpp>
#include <name.hpp>

namespace rv::lv2 {

static constexpr asize_t MAX_ALLOC_WRAPPER = 0x100; // bytes of code
// libstdc++: malloc, get_new_handler, the handler, bad_alloc allocate + throw
static constexpr int MAX_ALLOC_WRAPPER_CALLS = 6;

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

// var = alloc(N), then the first call that passes var first
struct alloc_finder_t : public ctree_visitor_t {
	int var;
	instance_t& out;

	alloc_finder_t(int v, instance_t& o)
		: ctree_visitor_t(CV_FAST), var(v), out(o) {}

	int idaapi visit_expr(cexpr_t* e) override {
		if (out.site == BADADDR) {
			if (e->op != cot_asg || !is_var(e->x, var)) return 0;

			cexpr_t* call = strip_casts(e->y);
			if (call->op != cot_call || call->a->empty()) return 0;

			cexpr_t* n = strip_casts(&call->a->at(0));
			const ea_t fn = direct_callee(call);
			if (n->op != cot_num || fn == BADADDR || !is_allocator(fn))
				return 0;

			out.size = size_t(n->numval());
			out.site = e->ea;
			out.alloc = fn;
			return 0;
		}

		if (e->op != cot_call || e->a->empty() || !is_var(&e->a->at(0), var))
			return 0;

		const ea_t fn = direct_callee(e);
		func_t* pfn = fn != BADADDR ? get_func(fn) : nullptr;
		if (pfn == nullptr || in_extern_seg(fn) || (pfn->flags & FUNC_LIB) != 0)
			return 0;

		out.ctor = fn;
		return 1; // done
	}
};

bool find_instance(ea_t inst, instance_t& out) {
	out = {};
	out.inst = inst;

	cfuncptr_t cf = rv::decompile_at(inst);
	if (cf == nullptr) return false;

	loc_t l;
	if (!rv::return_loc(cf, l) || l.off != 0 || l.var < 0) {
		rv_log("instantiate at %a returns no single local", inst);
		return false;
	}
	out.var = l.var;

	alloc_finder_t f(l.var, out);
	f.apply_to(&cf->body, nullptr);
	if (out.site == BADADDR) {
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
		if (out.get_size() != size)
			rv_log("%s exists with 0x%llX bytes, 0x%llX allocated; kept",
				type_name, (uint64)out.get_size(), (uint64)size);
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
}

} // namespace rv::lv2
