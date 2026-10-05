#pragma once
#include <hexrays.hpp>
#include <pro.h>
#include <kernwin.hpp>
#include <segment.hpp>

// Source: https://stackoverflow.com/questions/16388510
constexpr unsigned int str_cth(const char* str, int h = 0) {
	return !str[h] ? 5381 : (str_cth(str, h + 1) * 33) ^ str[h];
}

AS_PRINTF(1, 2) inline void rv_log(const char* fmt, ...) {
	qstring buf;
	va_list va;
	va_start(va, fmt);
	buf.vsprnt(fmt, va);
	va_end(va);
	msg("[REverb] %s\n", buf.c_str());
}

inline cexpr_t* strip_casts(cexpr_t* e) {
	while (e && e->op == cot_cast)
		e = e->x;
	return e;
}

inline qstring expr_str(const cexpr_t* e, const cfunc_t* cf) {
	qstring s;
	e->print1(&s, cf);
	tag_remove(&s);
	return s;
}

inline bool in_exec_seg(ea_t ea) {
	segment_t* s = getseg(ea);
	if (!s) return false;
	return (s->perm & SEGPERM_EXEC) != 0 || s->type == SEG_CODE;
}

inline bool is_code_addr(ea_t ea) {

	if (ea == BADADDR || !is_mapped(ea)) return false;

	flags64_t f = get_flags(ea);
	if (is_data(f)) return false;
	if (get_func(ea) != nullptr) return true;
	if (is_code(f)) return true;
	return in_exec_seg(ea); // not analyzed yet, but in a code segment
}