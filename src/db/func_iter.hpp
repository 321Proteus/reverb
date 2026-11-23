#pragma once
/**
 * @file func_iter.hpp
 * @brief Function iteration shims for IDA SDK 9.4.0+ and older SDKs.
 */
#include <pro.h>
#include <funcs.hpp>

#if IDA_SDK_VERSION >= 940

/** @brief Iterator over the items of a function */
typedef function_item_iterator_t rv_fii_t;

/** @brief Start address of the @p i-th function, or BADADDR */
inline ea_t rv_func_start(int i) { return get_func_ea_by_num(i); }

/** @brief Points @p it at the function containing @p ea; false if none */
inline bool rv_fii_set(rv_fii_t& it, ea_t ea) { return it.set(ea); }

#else

/** @brief Iterator over the items of a function */
typedef func_item_iterator_t rv_fii_t;

/** @brief Start address of the @p i-th function, or BADADDR */
inline ea_t rv_func_start(int i) {
	func_t* f = getn_func(i);
	return f ? f->start_ea : BADADDR;
}

/** @brief Points @p it at the function containing @p ea; false if none */
inline bool rv_fii_set(rv_fii_t& it, ea_t ea) {
	func_t* f = get_func(ea);
	return f && it.set(f);
}

#endif