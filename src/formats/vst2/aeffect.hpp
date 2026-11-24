#pragma once

#include "decomp/fields.hpp"
#include <hexrays.hpp>
#include <map>
#include <pro.h>
#include <vector>

/**
 * @file aeffect.hpp
 * @brief Extraction of constructed `AEffect` structs from decompiled code.
 */
namespace rv::vst2 {

/**
 * @brief An `AEffect` built by a function
 */
struct aeffect_hit_t : object_fields_t {
	ea_t func = BADADDR;	 /**< Function that builds it */
	ea_t magic_ea = BADADDR; /**< Address of the `magic` store */
};

/** @brief A member of the `AEffect` layout */
using aeffect_field_t = field_desc_t;

/** @brief The `AEffect` members that are looked for */
inline constexpr const char* members[] = {
	"dispatcher",
	"process",
	"setParameter",
	"getParameter",
	"numPrograms",
	"numParams",
	"numInputs",
	"numOutputs",
	"flags",
	"initialDelay",
	"ioRatio",
	"object",
	"uniqueID",
	"version",
	"processReplacing",
	"processDoubleReplacing",
};

/**
 * @brief Extracts the `AEffect` structs built by @p cf
 *
 * @param fields       Layout of `AEffect`, see load_layout()
 * @param aeffect_size Size of `AEffect` in bytes
 * @param cf          Decompiled function
 */
std::vector<aeffect_hit_t> extract_aeffects(
	const std::vector<aeffect_field_t>& fields, int aeffect_size, cfunc_t* cf);

/**
 * @brief Names the callbacks of @p h and types them
 *
 * @param fields   Layout of `AEffect`
 * @param h        The `AEffect` to apply
 * @param multiple Several `AEffect`s were found (shell plugin): names get the
 * `uniqueID` as a suffix
 */
void apply_aeffect(const std::vector<aeffect_field_t>& fields,
	const aeffect_hit_t& h, bool multiple);

} // namespace rv::vst2