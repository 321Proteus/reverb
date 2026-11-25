#pragma once

#include "ctree.hpp"

/**
 * @file sse.hpp
 * @brief SSE intrinsics as Hex-Rays prints them, matched on the ctree.
 */

/**
 * @brief Matches a store intrinsic, `_mm_storeu_si128(ptr, value)`, writing
 * @p size bytes at @p ptr
 */
bool sse_store(cexpr_t* e, cexpr_t*& ptr, cexpr_t*& value, int& size);

/**
 * @brief Matches `_mm_unpacklo_epi64(lo, hi)`: two 8-byte values packed into
 * one register
 */
bool sse_unpacklo64(cexpr_t* e, cexpr_t*& lo, cexpr_t*& hi);

/**
 * @brief The object a load intrinsic reads: all 16 bytes for
 * `_mm_load_si128(&xmmword_X)`, or with @p low the 8 of
 * `_mm_loadl_epi64(&off_X)`
 */
bool sse_load_src(cexpr_t* e, bool low, ea_t& src);
