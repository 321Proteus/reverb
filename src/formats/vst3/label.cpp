#include <pro.h>
#include <funcs.hpp>
#include <hexrays.hpp>
#include <name.hpp>
#include "core/log.hpp"
#include "vst3.hpp"
#include "finders.hpp"
#include "decomp/analysis.hpp"
#include "ui/report.hpp"

static bool may_be_factory(ea_t vt) {
	rv::rtti::class_info ci;
	if (!rv::rtti::parse_rtti(vt, &ci)) return true;

	for (const qstring& b : rv::rtti::bases_at(ci, ci.sub_offset))
		if (rv::rtti::short_name(b).starts_with("IPluginFactory")) return true;

	rv_log(
		"Skipping %s vtable at %a: not an IPluginFactory", ci.name.c_str(), vt);
	return false;
}

void rv::vst3::label_factory() {
	const ea_t gpf_ea = find_get_plugin_factory();
	if (gpf_ea == BADADDR) {
		rv_log("GetPluginFactory not found");
		return;
	}

	func_t* pfn = rv::ensure_func(gpf_ea);
	if (pfn == nullptr) {
		rv_log("Couldn't create a function at GetPluginFactory");
		return;
	}

	hexrays_failure_t hf;
	cfuncptr_t cfunc = decompile(pfn, &hf, DECOMP_NO_WAIT | DECOMP_NO_CACHE);

	if (cfunc == nullptr) {
		rv_log("Decompile failed: %s", hf.desc().c_str());
		return;
	}

	vtbl::store_finder_t vfinder;
	vfinder.apply_to(&cfunc->body, nullptr);

	bool is_direct = true;

	if (vfinder.hits.empty()) {

		rv_log("No direct hits, trying ctor candidates");
		is_direct = false;

		ctor_candidate_finder_t cfinder;
		cfinder.apply_to(&cfunc->body, nullptr);

		for (ea_t cand_ea : cfinder.candidates) {

			// Imports/extern thunks fail add_func and are skipped here
			func_t* cand_pfn = rv::ensure_func(cand_ea);
			if (!cand_pfn) continue;

			cfuncptr_t cand_cfunc =
				decompile(cand_pfn, &hf, DECOMP_NO_WAIT | DECOMP_NO_CACHE);
			if (!cand_cfunc) continue;

			vtbl::store_finder_t cand_finder;
			cand_finder.apply_to(&cand_cfunc->body, nullptr);

			for (auto& h : cand_finder.hits) {
				if (!h.this_offset) {
					h.ctor_ea = cand_ea;
					vfinder.hits.push_back(h);
				}
			}
		}
	}

	for (auto& h : vfinder.hits) {

		if (h.this_offset || !may_be_factory(h.vtable_ea)) continue;
		// clang-format off
		const qstring vtbl_type = vtbl::pick_type(
			h.vtable_ea, {
				"Steinberg::IPluginFactory3_vtbl",
				"Steinberg::IPluginFactory2_vtbl",
				"Steinberg::IPluginFactory_vtbl"
			});
		// clang-format on

		if (vtbl_type.empty()) {
			rv_log("Labelling vtable failed at %a", h.vtable_ea);
			continue;
		}

		vtbl::pin(h.vtable_ea, vtbl_type.c_str());
		rv_log("Found IPluginFactory vtable at %a", h.vtable_ea);

		qstring vt;
		vt.sprnt("%s (%a)", vtbl_type.c_str(), h.vtable_ea);

		rv::report::heading("VST3 factory object", h.vtable_ea);
		rv::report::row("vtable", vt, h.vtable_ea);

		ea_t global_ea = BADADDR;

		if (is_direct) {
			global_assign_finder_t gfinder(h.var_idx);
			gfinder.apply_to(&cfunc->body, nullptr);
			global_ea = gfinder.global_ea;
		} else {
			ctor_global_finder_t cgfinder(h.ctor_ea);
			cgfinder.apply_to(&cfunc->body, nullptr);
			global_ea = cgfinder.global_ea;
		}

		rv::report::addr_row("singleton", global_ea);

		if (global_ea == BADADDR) {
			rv_log("No global found for this factory");
			continue;
		};

		int idx = qisdigit(vtbl_type.at(14)) ? 15 : 14;
		qstring type = vtbl_type.substr(0, idx);

		tinfo_t ti;
		if (!ti.get_named_type(nullptr, type.c_str())) {
			rv_log("IPluginFactory type missing");
			continue;
		}

		tinfo_t ptr_ti;
		ptr_ti.create_ptr(ti);
		if (!apply_tinfo(global_ea, ptr_ti, TINFO_DEFINITE))
			rv_log("apply_tinfo on global %a failed", global_ea);
		else {
			rv_log("Typed factory singleton at %a", global_ea);
			mark_cfunc_dirty(gpf_ea);
		}
	}
}
