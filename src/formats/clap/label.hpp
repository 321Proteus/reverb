#pragma once

#include <pro.h>
#include <typeinf.hpp>

/**
 * @file label.hpp
 * @brief Naming and typing of function-pointer tables and their callbacks.
 * @internal Not part of the public API; used within the format module only.
 */
namespace rv::clap {
/** @cond INTERNAL */

/** @brief How a callback is referenced, see classify_fn() */
struct fn_ref_t {
	ea_t ea = BADADDR;
	bool keep_name = false; /**< Already named by someone else */
	bool shared = false;	/**< Referenced from elsewhere */
};

/**
 * @brief Whether @p fn is referenced from anywhere other than
 * `[owner, owner + owner_size)`
 */
fn_ref_t classify_fn(ea_t fn, ea_t owner, size_t owner_size);

/**
 * @brief Prototypes and names @p fn as `prefix__member`
 *
 * Shared and already-named functions are commented and skipped. A callback
 * that only forwards to another function also names that one
 * `prefix__member_impl`.
 *
 * @param fn         Function to label
 * @param m          Struct member the function was stored in
 * @param prefix     Name prefix, e.g. `clap_plugin`
 * @param owner      What references @p fn: the struct, or the function that
 * stores the pointer
 * @param owner_size Size of @p owner in bytes
 */
void label_fn(
	ea_t fn, const udm_t& m, const char* prefix, ea_t owner, size_t owner_size);

/**
 * @brief Types the struct at @p ea and labels every function member
 */
void label_fn_table(ea_t ea, const char* type, const char* prefix);

/** @brief @p s with non-alphanumerics replaced by `_` */
qstring c_ident(const qstring& s);
/** @endcond */

} // namespace rv::clap
