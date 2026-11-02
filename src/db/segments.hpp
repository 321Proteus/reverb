#pragma once
#include <pro.h>
#include <bytes.hpp>
#include <funcs.hpp>
#include <segment.hpp>

inline bool in_exec_seg(ea_t ea) {
	segment_t* s = getseg(ea);
	if (!s) return false;
	return (s->perm & SEGPERM_EXEC) != 0 || s->type == SEG_CODE;
}

// Imports (extern, IAT, Mach-O stub targets)
inline bool in_extern_seg(ea_t ea) {
	segment_t* s = getseg(ea);
	return s != nullptr && s->type == SEG_XTRN;
}

inline bool is_code_addr(ea_t ea) {

	if (ea == BADADDR || !is_mapped(ea)) return false;

	flags64_t f = get_flags(ea);
	if (is_data(f)) return false;
	if (get_func(ea) != nullptr) return true;
	if (is_code(f)) return true;
	return in_exec_seg(ea); // not analyzed yet, but in a code segment
}

namespace rv {

/*
	Initialized, non-code segments: constants, vtables, RTTI, string literals.
	Mach-O __TEXT data sections inherit R|X perms, so perms only decide
	when the segment type says nothing
*/
inline bool is_data_seg(const segment_t* s) {
	if (s == nullptr) return false;
	if (s->type == SEG_CODE || s->type == SEG_XTRN || s->type == SEG_BSS)
		return false;
	if (s->type == SEG_NORM && s->perm != 0)
		return (s->perm & SEGPERM_EXEC) == 0;
	return true;
}

inline bool in_data_seg(ea_t ea) {
	return is_loaded(ea) && is_data_seg(getseg(ea));
}

} // namespace rv
