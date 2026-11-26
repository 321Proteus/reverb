#pragma once
#include "core/reverb.hpp"
#include "minihost/host.hpp"
#include <memory>

/**
 * @file host.hpp
 * @brief What a plugin format provides to reverb-minihost, built into the
 * minihost only
 */

namespace rv {

/**
 * @brief What a plugin format provides to reverb-minihost
 */
struct HostModule {
	Interface iface;
	/// A fresh host for one plugin instance
	std::unique_ptr<host::PluginHost> (*create)();
};

/**
 * @brief The host module of @p iface, or null if the minihost can't host it
 */
const HostModule* get_host_module(Interface iface);

namespace vst2 {
extern const HostModule host_module;
}
namespace vst3 {
extern const HostModule host_module;
}
namespace clap {
extern const HostModule host_module;
}

} // namespace rv
