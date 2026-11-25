#pragma once
#include "core/reverb.hpp"
#include "frameworks/framework.hpp"

namespace rv {

/**
 * @file til.hpp
 * @brief Type library of the detected framework.
 */

/**
 * @brief Adds the type library of the detected framework version to the
 * database and copies it into the local types
 *
 * @param ifaces Interfaces found in the binary
 * @param info   Detected framework and version
 */
void apply_framework_types(
	const InterfaceSet& ifaces, const FrameworkInfo& info);

} // namespace rv
