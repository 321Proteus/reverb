#pragma once

#include <pro.h>
#include <name.hpp>

/**
 * @file vst3.hpp
 * @brief VST3 format module.
 */

namespace rv::vst3 {

/**
 * @brief `GetPluginFactory`, with or without the leading underscore
 *
 * @return Its address, or BADADDR
 */
inline ea_t find_get_plugin_factory() {
	const ea_t ea = get_name_ea(BADADDR, "GetPluginFactory");
	return ea != BADADDR ? ea : get_name_ea(BADADDR, "_GetPluginFactory");
}

/** @brief Names the factory, its classes and their `createFunc`s */
void label_factory();

/** @brief Finds `GetPluginFactory` and reads the factory and class infos it
 * builds */
void scan();
} // namespace rv::vst3
