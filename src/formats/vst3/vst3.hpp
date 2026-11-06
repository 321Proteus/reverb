#pragma once

#include <pro.h>
#include <name.hpp>

namespace rv::vst3 {

inline ea_t find_get_plugin_factory() {
	const ea_t ea = get_name_ea(BADADDR, "GetPluginFactory");
	return ea != BADADDR ? ea : get_name_ea(BADADDR, "_GetPluginFactory");
}

void label_factory();
void scan();
} // namespace rv::vst3
