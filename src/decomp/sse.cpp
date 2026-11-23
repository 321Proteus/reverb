/**
 * @file sse.cpp
 * @brief SSE intrinsic matchers, see sse.hpp.
 */
#include "sse.hpp"

/**
 * @brief Bytes written by a store intrinsic named @p n, or 0
 *
 * The upper-half stores (`_mm_storeh_*`) write at p, but not the low half of the
 * value, so they are not modelled.
 */
static int store_size(const char* n) {
	for (const char* w : { "_mm_store_si128", "_mm_storeu_si128",
			 "_mm_store_ps", "_mm_storeu_ps", "_mm_store_pd", "_mm_storeu_pd" })
		if (strcmp(n, w) == 0) return 16;

	for (const char* w : { "_mm_storel_epi64", "_mm_storel_pd", "_mm_store_sd",
			 "_mm_storel_pi" })
		if (strcmp(n, w) == 0) return 8;

	return strcmp(n, "_mm_store_ss") == 0 ? 4 : 0;
}

bool sse_store(cexpr_t* e, cexpr_t*& ptr, cexpr_t*& value, int& size) {
	if (e == nullptr || e->op != cot_call || e->a == nullptr ||
		e->a->size() != 2)
		return false;

	cexpr_t* fn = strip_casts(e->x);
	if (fn->op != cot_helper || (size = store_size(fn->helper)) == 0)
		return false;

	ptr = &e->a->at(0);
	value = &e->a->at(1);
	return true;
}

bool sse_unpacklo64(cexpr_t* e, cexpr_t*& lo, cexpr_t*& hi) {
	e = strip_casts(e);
	if (!is_helper_call(e, "_mm_unpacklo_epi64", 2)) return false;

	lo = &e->a->at(0);
	hi = &e->a->at(1);
	return true;
}

bool sse_load_src(cexpr_t* e, bool low, ea_t& src) {
	e = strip_casts(e);
	const bool load = low ? is_helper_call(e, "_mm_loadl_epi64", 1)
						  : is_helper_call(e, "_mm_load_si128", 1) ||
								is_helper_call(e, "_mm_loadu_si128", 1);
	if (!load) return false;

	cexpr_t* a = strip_casts(&e->a->at(0));
	if (a->op == cot_ref) a = strip_casts(a->x);
	if (a->op != cot_obj) return false;

	src = a->obj_ea;
	return true;
}
