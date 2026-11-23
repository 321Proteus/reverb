#pragma once
#include <pro.h>
#include <funcs.hpp>
#include <hexrays.hpp>

/**
 * @file analysis.hpp
 * @brief On-demand analysis helpers: analyze only what a format module needs,
 * and store results so neither the auto-analyzer nor Hex-Rays overrides them.
 */
namespace rv {

/**
 * @brief Creates (if needed) and analyzes a single function, without draining
 * the whole auto-analysis queue
 *
 * @param ea Any address inside the function, or its start if it does not exist
 * yet
 * @return The function, or nullptr if none can be made
 */
func_t* ensure_func(ea_t ea);

/**
 * @brief ensure_func() and decompile
 *
 * @param ea    Any address inside the function
 * @param flags Hex-Rays `DECOMP_*` flags; the default does not wait for
 * auto-analysis
 * @return The decompiled function; logs and returns nullptr on failure
 */
cfuncptr_t decompile_at(ea_t ea, int flags = DECOMP_NO_WAIT);

/**
 * @brief decompile_at(), but when Hex-Rays guessed fewer than @p min_args
 * arguments (float args in xmm, a misdetected `__thiscall`), retypes the
 * function from the prototype at a call site that passes them all and
 * decompiles again
 *
 * Never overrides a user-set type.
 *
 * @param ea       Function to decompile
 * @param min_args Number of arguments the function must end up with
 * @param site     Type of the call site's callee (function or function
 * pointer) carrying all the arguments; null to skip the retyping
 * @return The decompiled function, retyped if that was needed and possible
 */
cfuncptr_t decompile_with_args(ea_t ea, int min_args, const tinfo_t* site);

/**
 * @brief Follows jump and this-adjusting thunks, at most @p max_hops times
 *
 * @param ea       Function or thunk address
 * @param max_hops Maximum number of thunks to follow
 * @return The final target, or @p ea itself if it is not a thunk
 */
ea_t resolve_thunk(ea_t ea, int max_hops = 3);

/**
 * @brief The constant of a function that decompiles to just `return <const>;`
 *
 * @param ea  Function address
 * @param out Receives the constant
 * @return false if the function is not of that form
 */
bool const_return(ea_t ea, uint64* out);

/**
 * @brief The one call of a function that only forwards: `return f(...);` or
 * `f(...); return;`
 *
 * @param cf Decompiled function, may be null
 * @return The call, or nullptr
 */
cexpr_t* tail_call(cfunc_t* cf);

/**
 * @brief The callee of a function that only forwards: `return f(...);` or
 * `f(...); return;`
 *
 * @param ea       Function address
 * @param indirect Set when the one call goes through a pointer (virtual call,
 * function pointer member), may be null
 * @return The callee with thunks resolved, or BADADDR if the function does not
 * forward a direct call
 */
ea_t direct_tail_callee(ea_t ea, bool* indirect = nullptr);

/**
 * @brief Parses a C declaration and applies it as a user-defined (definite)
 * type
 *
 * @param ea   Address to type
 * @param decl C declaration, e.g. `int __cdecl f(int a)`
 * @return false if the declaration does not parse or cannot be applied
 */
bool apply_definite_proto(ea_t ea, const char* decl);

/**
 * @brief Persistently renames and/or retypes a decompiler local variable
 *
 * @param func_ea Function containing the variable
 * @param v       Variable to modify, from that function's decompilation
 * @param name    Null to keep the name
 * @param type    Null to keep the type
 * @return false if Hex-Rays refused the change
 */
bool set_lvar(
	ea_t func_ea, const lvar_t& v, const char* name, const tinfo_t* type);

} // namespace rv
