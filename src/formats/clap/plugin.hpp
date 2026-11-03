#pragma once

#include <pro.h>
#include <vector>
#include "descriptor.hpp"

namespace rv::clap {

/*
	Find the clap_plugin_t instances that create_plugin builds and label
	their callbacks, then the extensions that get_extension returns. An
	instance is found from the store of a known descriptor, else from the
	callback stores in create_plugin and the functions it calls, else as a
	static template it references
*/
void label_plugins(ea_t create_plugin_ea, const std::vector<desc_t>& descs);

} // namespace rv::clap
