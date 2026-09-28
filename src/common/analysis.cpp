#include "analysis.hpp"
#include <auto.hpp>
#include <typeinf.hpp>

namespace rv {

func_t* ensure_func(ea_t ea) {
	if (get_func(ea) == nullptr && !add_func(ea)) return nullptr;

	// Function bounds can grow as code is discovered, so repeat until stable
	ea_t prev_end;
	func_t* pfn;
	do {
		pfn = get_func(ea);
		if (pfn == nullptr) return nullptr;
		prev_end = pfn->end_ea;
		auto_wait_range(pfn->start_ea, pfn->end_ea);
		pfn = get_func(ea);
	} while (pfn != nullptr && pfn->end_ea != prev_end);

	return pfn;
}

bool apply_definite_proto(ea_t ea, const char* decl) {
	tinfo_t tif;
	if (!parse_decl(&tif, nullptr, nullptr, decl, PT_SIL)) return false;
	return apply_tinfo(ea, tif, TINFO_DEFINITE);
}

bool set_lvar(
	ea_t func_ea, const lvar_t& v, const char* name, const tinfo_t* type) {
	lvar_saved_info_t lsi;
	lsi.ll = v;
	uint flags = 0;

	if (name) {
		lsi.name = name;
		flags |= MLI_NAME;
	}

	if (type) {
		lsi.type = *type;
		flags |= MLI_TYPE;
	}

	return modify_user_lvar_info(func_ea, flags, lsi);
}

} // namespace rv
