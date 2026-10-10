#pragma once
#include <pro.h>
#include <funcs.hpp>
#include <hexrays.hpp>

// On-demand analysis helpers: analyze only what a format module needs, and
// store results so neither the auto-analyzer nor Hex-Rays overrides them.
namespace rv {

// Create (if needed) and analyze a single function, without draining the
// whole auto-analysis queue. Returns nullptr if no function can be made.
func_t* ensure_func(ea_t ea);

// ensure_func + decompile. logs and returns nullptr on failure
cfuncptr_t decompile_at(ea_t ea, int flags = DECOMP_NO_WAIT);

/*
	decompile_at, but when Hex-Rays guessed fewer than min_args arguments
	(float args in xmm, a misdetected __thiscall), retype the function from
	the prototype at a call site that passes them all and decompile again.
	Never overrides a user-set type.
*/
cfuncptr_t decompile_with_args(ea_t ea, int min_args, const tinfo_t* site);

// Follow jump and this-adjusting thunks
ea_t resolve_thunk(ea_t ea, int max_hops = 3);

// Parse a C declaration and apply it as a user-defined (definite) type.
bool apply_definite_proto(ea_t ea, const char* decl);

// Persistently rename and/or retype a decompiler local variable.
bool set_lvar(
	ea_t func_ea, const lvar_t& v, const char* name, const tinfo_t* type);

} // namespace rv
