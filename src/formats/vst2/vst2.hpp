#pragma once

#include <cstdint>
#include <pro.h>

/**
 * @file vst2.hpp
 * @brief VST2 format module.
 */

/** @brief `AEffect::magic`, `'VstP'` */
static constexpr uint32_t VST2_MAGIC = 0x56737450;

namespace rv::vst2 {
/**
 * @brief `VSTPluginMain` or one of its aliases
 *
 * @return Its address, or BADADDR if there is none
 */
ea_t find_entry();

/**
 * @brief Whether @p ea is a pre-2.4 `main` entry, checked by decompiling it
 * with the entry prototype and looking for the `AEffect` it builds
 *
 * The previous type of @p ea is restored if the check fails.
 */
bool check_vst2_ep(ea_t ea);
/** @brief Names the entry point and types the `AEffect` it returns */
void label_entry();

/** @brief Finds the entry point, the `AEffect` instances and the dispatcher */
void scan();
} // namespace rv::vst2