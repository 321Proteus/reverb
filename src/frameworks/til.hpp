#pragma once
#include "core/reverb.hpp"
#include "frameworks/framework.hpp"

namespace rv {

// The type library of the detected framework version
// added to the database and copied into the local types
void apply_framework_types(
	const InterfaceSet& ifaces, const FrameworkInfo& info);

} // namespace rv
