#include "detect.hpp"
#include "reverb.hpp"
#include <hexrays.hpp>
#include <pro.h>
#include <vector>
#include "util.hpp"
#include <typeinf.hpp>
#include "common/analysis.hpp"

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

// void* return: the VST2 types (AEffect) aren't parsed until detection succeeds
static const char* kProbeDecl =
	"void *__cdecl vst2_entry(__int64 (__cdecl *audioMaster)"
	"(void *effect, int opcode, int index, __int64 value, void *ptr, float "
	"opt));";

bool check_vst2_ep(ea_t ea) {
	func_t* pfn = rv::ensure_func(ea);
	if (pfn == nullptr) {
		rv_log("couldn't create a function at %a", ea);
		return false;
	}

	// The probe prototype is applied before we know this is VST2, so remember
	// the previous type and restore it if the check fails
	tinfo_t old_tif;
	bool had_type = get_tinfo(&old_tif, pfn->start_ea);

	if (!rv::apply_definite_proto(pfn->start_ea, kProbeDecl)) {
		rv_log("couldn't apply the VST2 probe prototype at %a", pfn->start_ea);
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

	return found;
}

static const char* lstrip(const char* s) { return (s[0] == '_') ? s + 1 : s; }

rv::Interface classify_from_exports(const std::vector<qstring>& exports) {

	for (const qstring& ex : exports) {
		switch (str_cth(lstrip(ex.c_str()))) {

		case str_cth("VSTPluginMain"):
			return rv::VST2;
		case str_cth("GetPluginFactory"):
			return rv::VST3;
		case str_cth("ACFRegisterComponent"):
			return rv::AAX;
		case str_cth("clap_entry"):
			return rv::CLAP;
		case str_cth("AUPluginInstanceEntry"):
			return rv::AU;
		}
	}

	return rv::OTHER;
}