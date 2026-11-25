#pragma once

#include <pro.h>
#include <cstring>

/**
 * @file exts.hpp
 * @brief Tables of CLAP factory and extension ids, generated from `exts.inc`.
 * @internal Not part of the public API; used within the format module only.
 */
namespace rv::clap {
/** @cond INTERNAL */

/** @brief A CLAP id and the struct type it names */
struct id_type_t {
	const char* name; /**< Macro name, e.g. `CLAP_PLUGIN_FACTORY_ID` */
	const char* id;	  /**< The id string */
	const char* type; /**< Struct type, `"NONE"` for host-only extensions */
};

/** @brief Factory ids */
inline constexpr id_type_t CLAP_FACTORIES[] = {

#define RV_CLAP_FACTORY(n, id, s) { #n, id, #s },
#define RV_CLAP_EXT(n, id, p, h)

#include "exts.inc"

#undef RV_CLAP_FACTORY
#undef RV_CLAP_EXT

};

/** @brief Plugin extension ids */
inline constexpr id_type_t CLAP_EXTS[] = {

#define RV_CLAP_FACTORY(n, id, s)
#define RV_CLAP_EXT(n, id, p, h) { #n, id, #p },

#include "exts.inc"

#undef RV_CLAP_FACTORY
#undef RV_CLAP_EXT

};

/** @brief Entry of @p table with id @p id, or null */
template <size_t N>
const id_type_t* by_id(const id_type_t (&table)[N], const char* id) {
	for (const id_type_t& k : table)
		if (strcmp(k.id, id) == 0) return &k;
	return nullptr;
}

/** @brief A `_COMPAT` id and its stable id name the same struct; prefers the
 * stable one */
inline const id_type_t* prefer(const id_type_t* a, const id_type_t* b) {
	const bool a_compat = qstring(a->name).ends_with("_COMPAT");
	const bool b_compat = qstring(b->name).ends_with("_COMPAT");
	return a_compat && !b_compat ? b : a;
}
/** @endcond */

} // namespace rv::clap