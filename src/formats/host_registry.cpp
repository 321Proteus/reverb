/**
 * @file host_registry.cpp
 * @brief The host-side equivalent of registry.cpp: table of the formats
 * reverb-minihost can host
 */
#include "host.hpp"

namespace rv {

/**
 * @brief Match the detected interface with a host module
 */
const HostModule* get_host_module(Interface iface) {
	switch (iface) {
	case Interface::VST2: return &vst2::host_module;
	case Interface::VST3: return &vst3::host_module;
	case Interface::CLAP: return &clap::host_module;
	default:			  return nullptr;
	}
}

} // namespace rv
