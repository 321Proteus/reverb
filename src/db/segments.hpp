#pragma once
/**
 * @file segments.hpp
 * @brief Segment classification helpers (code, data, imports, metadata, GOT).
 */
#include <pro.h>
#include <bytes.hpp>
#include <funcs.hpp>
#include <segment.hpp>

/** @brief Whether @p ea is in an executable or code segment */
inline bool in_exec_seg(ea_t ea) {
	segment_t* s = getseg(ea);
	if (!s) return false;
	return (s->perm & SEGPERM_EXEC) != 0 || s->type == SEG_CODE;
}

/** @brief Whether @p ea is in an import segment (extern, IAT, Mach-O stub
 * targets) */
inline bool in_extern_seg(ea_t ea) {
	segment_t* s = getseg(ea);
	return s != nullptr && s->type == SEG_XTRN;
}

/** @brief Whether @p ea is in an unwind or exception table section */
static bool in_metadata_seg(ea_t ea) {
	segment_t* s = getseg(ea);
	if (!s) return false;

	qstring n;
	get_segm_name(&n, s);

	static const char* const META_SECTIONS[] = {
		// PE64 unwind
		".pdata",
		".xdata",
		// ELF
		".eh_frame",
		".eh_frame_hdr",
		".gcc_except_table",
		// Mach O
		"__unwind_info",
		"__eh_frame",
		"__gcc_except_tab",
	};

	for (const char* m : META_SECTIONS)
		if (n == m) return true;
	return false;
}

/** @brief Whether @p ea is in a pointer section the loader resolves: ELF GOT,
 * Mach-O symbol pointers */
inline bool in_got_seg(ea_t ea) {
	segment_t* s = getseg(ea);
	if (!s) return false;

	qstring n;
	get_segm_name(&n, s);

	static const char* const GOT_SECTIONS[] = {
		".got",
		".got.plt",
		"__got",
		"__la_symbol_ptr",
		"__nl_symbol_ptr",
		"__auth_got",
	};

	for (const char* g : GOT_SECTIONS)
		if (n == g) return true;
	return false;
}

/**
 * @brief Whether @p ea is code: a function, a code item, or an address in a
 * code segment that is not analyzed yet
 */
inline bool is_code_addr(ea_t ea) {

	if (ea == BADADDR || !is_mapped(ea)) return false;

	flags64_t f = get_flags(ea);
	if (is_data(f)) return false;
	if (get_func(ea) != nullptr) return true;
	if (is_code(f)) return true;
	return in_exec_seg(ea); // not analyzed yet, but in a code segment
}

namespace rv {

/**
 * @brief Whether @p s is an initialized, non-code segment: constants, vtables,
 * RTTI, string literals
 *
 * Mach-O `__TEXT` data sections inherit R|X perms, so perms only decide when
 * the segment type says nothing.
 */
inline bool is_data_seg(const segment_t* s) {
	if (s == nullptr) return false;
	if (s->type == SEG_CODE || s->type == SEG_XTRN || s->type == SEG_BSS)
		return false;
	if (s->type == SEG_NORM && s->perm != 0)
		return (s->perm & SEGPERM_EXEC) == 0;
	return true;
}

/** @brief Whether @p ea is loaded and in a data segment, see is_data_seg() */
inline bool in_data_seg(ea_t ea) {
	return is_loaded(ea) && is_data_seg(getseg(ea));
}

} // namespace rv
