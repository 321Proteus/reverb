#pragma once
#include <pro.h>
#include <hexrays.hpp>
#include <lines.hpp>

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
