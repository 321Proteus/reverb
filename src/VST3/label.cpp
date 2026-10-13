#include <pro.h>
#include <funcs.hpp>
#include <hexrays.hpp>
#include <name.hpp>
#include "VST3/vst3.hpp"
#include "VST3/finders.hpp"
#include "common/analysis.hpp"
#include "common/vtable.hpp"
#include "util.hpp"

void rv::vst3::label_factory() {
	ea_t gpf_ea = get_name_ea(BADADDR, "GetPluginFactory");
	if (gpf_ea == BADADDR) {
		gpf_ea = get_name_ea(BADADDR, "_GetPluginFactory");

		if (gpf_ea == BADADDR) {
			rv_log("GetPluginFactory not found");
			return;
		}
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

	vtable_store_finder_t vfinder;
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

			vtable_store_finder_t cand_finder;
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
		
		if (h.this_offset) continue;
			const char* type = pick_vtable_type(h.vtable_ea, { "IPluginFactory3_vtbl", "IPluginFactory2_vtbl", "IPluginFactory_vtbl" });

		if (type == nullptr || !label_vtable(h.vtable_ea, type)) {
			rv_log("label_vtable failed at %a", h.vtable_ea);
			continue;
		} else {
			rv_log("Found IPluginFactory vtable at %a", h.vtable_ea);
		}

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

		if (global_ea == BADADDR) {
			rv_log("No global found for this factory");
			continue;
		}

		tinfo_t ti;
		if (!ti.get_named_type(nullptr, "IPluginFactory")) {
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

	// TODO: locate the PClassInfo2 array from getClassInfo (factory vtable
	// slot) instead of a per-sample hard-coded address
}
