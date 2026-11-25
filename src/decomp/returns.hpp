#pragma once
#include "stores.hpp"
#include <hexrays.hpp>
#include <pro.h>

/**
 * @file returns.hpp
 * @brief Entry points that return a pointer into an object they built: VST3
 * `createInstance` returns an interface subobject, a VST2 entry returns
 * `&wrapper->effect`, a CLAP `create_plugin` `&plugin->clap_plugin`.
 *
 * With the right return type set, the object local still has whatever type
 * Hex-Rays guessed, so `obj + 0xC0` shows as an index scaled by that type.
 */
namespace rv {

/**
 * @brief Where every non-null return of @p cf points: (object local, byte
 * offset)
 *
 * Follows locals assigned once from an address (`result = v2 + 24; return
 * result;`) and constant steps of the returned local (`if ( result )
 * ++result;`). Null assignments and `return nullptr` are the null path.
 *
 * @param cf  Decompiled function
 * @param out Receives the object local and the offset
 * @return false when the returns disagree or a returned local is assigned more
 * than once
 */
bool return_loc(cfunc_t* cf, loc_t& out);

/**
 * @brief Types the object local that @p fn returns plus an offset as `char *`,
 * so the adjustment reads in bytes
 *
 * @p fn must have its return type set already. A local the user typed is left
 * alone.
 *
 * @param fn     Function whose return pointer is adjusted
 * @param label  Name used in log messages
 * @param follow When @p fn only forwards (`return g(...);`), how many direct
 * callees to follow
 */
void type_return_base(ea_t fn, const char* label, int follow = 0);

} // namespace rv
