#pragma once

/**
 * @file reconstruct.hpp
 * @brief Entry point of vtable reconstruction.
 */

#include <pro.h>

namespace rv::vtbl {

/**
 * @brief Forces the interface of a vtable in the next reconstruction, for
 * what format modules know without RTTI
 *
 * @param vt        Vtable address point
 * @param vtbl_type Name of the `_vtbl` type
 */
void pin(ea_t vt, const char* vtbl_type);

/**
 * @brief Finds the RTTI vtables, adds the pinned ones, types them and labels
 * their slots using the loaded type decls
 */
void reconstruct();

} // namespace rv::vtbl
