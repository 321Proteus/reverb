#include "handles.hpp"
#include "core/log.hpp"
#include "db/read.hpp"
#include "db/segments.hpp"
#include "decomp/analysis.hpp"
#include "decomp/ctree.hpp"
#include "formats/fn_table.hpp"
#include <map>
#include <set>
#include <typeinf.hpp>

namespace rv::lv2 {

static bool is_handle(const tinfo_t& t) {
	qstring n;
	return t.get_type_name(&n) && (n == "LV2_Handle" || n == "LV2UI_Handle");
}

// (function, argument) -> the instance types it would get
typedef std::map<std::pair<ea_t, int>, std::set<qstring>> plan_t;

static void plan_table(const handle_use_t& u, plan_t& plan) {
	tinfo_t tif;
	udt_type_data_t udt;
	if (!tif.get_named_type(nullptr, u.table_type.c_str()) ||
		!tif.get_udt_details(&udt))
		return;

	for (const udm_t& m : udt) {
		if (!m.type.is_funcptr()) continue;

		const ea_t fn = read_ptr(u.table + m.offset / 8);
		if (fn == 0 || !is_code_addr(fn) || in_extern_seg(fn)) continue;

		func_type_data_t fi;
		if (!m.type.get_pointed_object().get_func_details(&fi)) continue;

		for (size_t i = 0; i < fi.size(); i++) {
			if (!is_handle(fi[i].type)) continue;

			if (classify_fn(fn, u.table, tif.get_size()).shared) {
				rv_log("%s::%s at %a is shared, handle left untyped",
					u.table_type.c_str(), m.name.c_str(), fn);
				continue;
			}
			plan[{ fn, int(i) }].insert(u.instance_type);
		}
	}
}

/*
	The function fn only forwards to, when it passes its argument idx
	there in the same position, else BADADDR
*/
static ea_t forwards_arg(ea_t fn, int idx) {
	cfuncptr_t cf = rv::decompile_at(fn);
	if (cf == nullptr || idx >= int(cf->argidx.size())) return BADADDR;

	cexpr_t* call = rv::tail_call(cf);
	if (call == nullptr || idx >= int(call->a->size())) return BADADDR;

	cexpr_t* callee = strip_casts(call->x);
	cexpr_t* a = strip_casts(&call->a->at(idx));
	if (callee->op != cot_obj || a->op != cot_var ||
		a->v.idx != cf->argidx[idx])
		return BADADDR;

	const ea_t impl = rv::resolve_thunk(callee->obj_ea);
	func_t* pfn = get_func(fn);
	if (impl == fn || pfn == nullptr ||
		classify_fn(impl, pfn->start_ea, pfn->size()).shared)
		return BADADDR;
	return impl;
}

static bool type_arg(ea_t fn, int idx, const tinfo_t& ptr) {
	if (!rv::set_arg_type(fn, idx, ptr)) {
		rv_log("Could not type argument %d of %a", idx, fn);
		return false;
	}
	mark_cfunc_dirty(fn);
	return true;
}

void type_handles(const std::vector<handle_use_t>& uses) {
	plan_t plan;
	for (const handle_use_t& u : uses)
		plan_table(u, plan);

	int typed = 0;
	for (const auto& [at, types] : plan) {
		const auto [fn, idx] = at;
		if (types.size() != 1) {
			rv_log("%a gets the handles of %u instances, left untyped", fn,
				unsigned(types.size()));
			continue;
		}

		tinfo_t st, ptr;
		if (!st.get_named_type(get_idati(), types.begin()->c_str())) continue;
		ptr.create_ptr(st);

		if (!type_arg(fn, idx, ptr)) continue;
		typed++;

		const ea_t impl = forwards_arg(fn, idx);
		if (impl != BADADDR && plan.count({ impl, idx }) == 0 &&
			type_arg(impl, idx, ptr))
			typed++;
	}
	rv_log("Typed %d LV2 handle argument(s)", typed);
}

} // namespace rv::lv2
