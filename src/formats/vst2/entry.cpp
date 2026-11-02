#include "core/log.hpp"
#include "decomp/ctree.hpp"
#include "vst2.hpp"
#include "decomp/analysis.hpp"
#include <auto.hpp>
#include <funcs.hpp>
#include <hexrays.hpp>
#include <ida.hpp>
#include <name.hpp>
#include <pro.h>
#include <search.hpp>
#include <bytes.hpp>
#include <segment.hpp>
#include <ua.hpp>

namespace rv::vst2 {

struct vst_entry_finder_t : public ctree_visitor_t {

	bool found = false;
	vst_entry_finder_t() : ctree_visitor_t(CV_FAST) {}

	int idaapi visit_expr(cexpr_t* e) override {
		if (e->op != cot_call) return 0;

		cexpr_t* callee = strip_casts(e->x);

		if (callee->op != cot_var || callee->v.idx != 0) return 0;

		carglist_t* args = e->a;
		if (args != nullptr && args->size() >= 2 && args->at(1).op == cot_num &&
			args->at(1).numval() == 1)
			found = true;

		return 0;
	}
};

static const char* entry_decl =
	"AEffect *__cdecl vst2_entry(audioMasterCallback audioMaster);";

static ea_t probed_ea = BADADDR;

bool check_vst2_ep(ea_t ea) {
	func_t* pfn = rv::ensure_func(ea);
	if (pfn == nullptr) {
		rv_log("couldn't create a function at %a", ea);
		return false;
	}

	// The prototype is applied before we know this is VST2, so remember the
	// previous type and restore it if the check fails
	tinfo_t old_tif;
	bool had_type = get_tinfo(&old_tif, pfn->start_ea);

	if (!rv::apply_definite_proto(pfn->start_ea, entry_decl)) {
		rv_log("couldn't apply the VST2 entry prototype at %a", pfn->start_ea);
		return false;
	}

	hexrays_failure_t hf;
	cfuncptr_t cfunc = decompile(pfn, &hf, DECOMP_NO_WAIT | DECOMP_NO_CACHE);

	bool found = false;
	if (cfunc != nullptr) {
		vst_entry_finder_t finder;
		finder.apply_to(&cfunc->body, nullptr);
		found = finder.found;
	} else {
		rv_log("Decompile failed: %s", hf.desc().c_str());
	}

	if (!found) {
		if (had_type) apply_tinfo(pfn->start_ea, old_tif, TINFO_GUESSED);
		else
			del_tinfo(pfn->start_ea);
		mark_cfunc_dirty(pfn->start_ea);
	}

	if (found) probed_ea = pfn->start_ea;
	return found;
}

ea_t find_entry() {
	static const char* const kEntryNames[] = { "VSTPluginMain",
		"_VSTPluginMain", "main" };

	for (const char* name : kEntryNames) {
		const ea_t ea = get_name_ea(BADADDR, name);
		if (ea != BADADDR) return ea;
	}
	return BADADDR;
}

void label_entry() {
	const ea_t entry_ea = find_entry();
	if (entry_ea == BADADDR) {
		rv_log("No valid VST2 entry point found");
		return;
	}

	func_t* pfn = rv::ensure_func(entry_ea);
	if (pfn == nullptr) {
		rv_log("Couldn't create function at VST2 entry");
		return;
	}

	// Already typed by the probe
	if (pfn->start_ea != probed_ea) {
		if (!rv::apply_definite_proto(pfn->start_ea, entry_decl)) {
			rv_log("Couldn't apply the prototype on VST2 entry");
			return;
		}
		mark_cfunc_dirty(pfn->start_ea);
	}
	rv_log("Typed VST2 entry point at %a", pfn->start_ea);
}
} // namespace rv::vst2
