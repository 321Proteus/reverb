#pragma once

#include <bytes.hpp>
#include <funcs.hpp>
#include <pro.h>
#include "exts.hpp"

/**
 * @file factory.hpp
 * @brief Finding the factories returned by `clap_entry.get_factory`.
 * @internal Not part of the public API; used within the format module only.
 */
namespace rv::clap {
/** @cond INTERNAL */

/** @brief A factory found in the binary */
struct factory_hit_t {
	ea_t ea = BADADDR;
	const id_type_t* kind = nullptr; /**< Which factory it is */
	bool assumed =
		false; /**< True if the kind is guessed, not seen in a compared id */
};

/**
 * @brief Whether the struct of type @p tif at @p ea is a table of local
 * functions
 */
bool is_fn_table(ea_t ea, const tinfo_t& tif);

/**
 * @brief is_fn_table() at @p ea, or one pointer hop away (GOT slot); updates
 * @p ea to the table
 */
bool resolve_table(ea_t& ea, const tinfo_t& tif);

/**
 * @brief Factories returned by `get_factory` at @p gf_ea
 */
std::vector<factory_hit_t> find_factories(ea_t gf_ea);
/** @endcond */

} // namespace rv::clap