#pragma once

/**
 * @file ctree.hpp
 * @brief Small ctree helpers shared by the decompiler analyses.
 */
#include <pro.h>
#include <hexrays.hpp>
#include <lines.hpp>
#include <cstring>

/**
 * @brief Skips casts: the first non-cast expression under @p e, null-safe
 */
inline cexpr_t* strip_casts(cexpr_t* e) {
	while (e && e->op == cot_cast)
		e = e->x;
	return e;
}

/**
 * @brief Pseudocode text of @p e without color tags
 */
inline qstring expr_str(const cexpr_t* e, const cfunc_t* cf) {
	qstring s;
	e->print1(&s, cf);
	tag_remove(&s);
	return s;
}

/**
 * @brief Whether @p e is a call of the compiler helper (intrinsic) @p name
 * with @p nargs arguments
 */
inline bool is_helper_call(cexpr_t* e, const char* name, size_t nargs) {
	if (e == nullptr || e->op != cot_call || e->a == nullptr ||
		e->a->size() != nargs)
		return false;

	cexpr_t* fn = strip_casts(e->x);
	return fn->op == cot_helper && strcmp(fn->helper, name) == 0;
}
