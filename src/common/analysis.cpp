#include "analysis.hpp"
#include "util.hpp"
#include <auto.hpp>
#include <ua.hpp>
#include <xref.hpp>
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

cfuncptr_t decompile_at(ea_t ea, int flags) {
	func_t* f = ensure_func(ea);

	if (f == nullptr) {
		rv_log("No function at %a", ea);
		return cfuncptr_t(static_cast<cfunc_t*>(nullptr));
	}

	hexrays_failure_t hf;
	cfuncptr_t cf = decompile(f, &hf, flags);
	if (cf == nullptr)
		rv_log("Failed to decompile %a: %s", f->start_ea, hf.desc().c_str());
	return cf;
}

static ea_t jump_stub_target(func_t* f) {
	constexpr int MAX_STUB_INSNS = 3;
	constexpr asize_t MAX_STUB_SIZE = 16;
	if (f->size() > MAX_STUB_SIZE) return BADADDR;

	ea_t ea = f->start_ea;
	for (int n = 0; n < MAX_STUB_INSNS && ea < f->end_ea; n++) {
		xrefblk_t xb;

		for (bool ok = xb.first_from(ea, XREF_NOFLOW); ok;
			ok = xb.next_from()) {

			if (!xb.iscode) continue;
			if (xb.type == fl_CN || xb.type == fl_CF) return BADADDR;
			if ((xb.type == fl_JN || xb.type == fl_JF) &&
				!func_contains(f, xb.to))
				return xb.to;
		}

		insn_t insn;
		if (decode_insn(&insn, ea) <= 0) break;

		ea += insn.size;
	}
	return BADADDR;
}

ea_t resolve_thunk(ea_t ea, int max_hops) {

	for (int i = 0; i < max_hops && ea != BADADDR; i++) {
		func_t* f = ensure_func(ea);

		if (f == nullptr) break;

		ea_t t = BADADDR;
		if ((f->flags & FUNC_THUNK) != 0) {
			ea_t fptr;
			t = calc_thunk_func_target(f, &fptr);
		}

		if (t == BADADDR) t = jump_stub_target(f);
		if (t == BADADDR || !is_code_addr(t)) break;

		ea = t;
	}

	return ea;
}

bool apply_definite_proto(ea_t ea, const char* decl) {
	tinfo_t tif;
	if (!parse_decl(&tif, nullptr, get_idati(), decl, PT_SIL)) {
		rv_log("Failed to parse prototype %s", decl);
		return false;
	}
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
