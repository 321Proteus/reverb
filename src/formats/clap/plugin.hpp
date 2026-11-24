#pragma once

#include <pro.h>
#include <vector>
#include "descriptor.hpp"

/**
 * @file plugin.hpp
 * @brief Labeling of `clap_plugin_t` instances and their extensions.
 * @internal Not part of the public API; used within the format module only.
 */
namespace rv::clap {
/** @cond INTERNAL */

/**
 * @brief Finds the `clap_plugin_t` instances that `create_plugin` builds and
 * labels their callbacks, then the extensions that `get_extension` returns
 *
 * An instance is found from the store of a known descriptor, else from the
 * callback stores in `create_plugin` and the functions it calls, else as a
 * static template it references.
 *
 * @param create_plugin_ea `create_plugin` of the factory
 * @param descs            Descriptors of the plugin
 */
void label_plugins(ea_t create_plugin_ea, const std::vector<desc_t>& descs);
/** @endcond */

} // namespace rv::clap
