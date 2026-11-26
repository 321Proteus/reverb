#pragma once
#include "core/reverb.hpp"
#include "platform.hpp"
#include <cstdarg>
#include <cstdio>
#include <memory>
#include <optional>
#include <string>

/**
 * @file host.hpp
 * @brief The per-format host interface of reverb-minihost.
 */

#if defined(__GNUC__) || defined(__clang__)
__attribute__((format(printf, 1, 2)))
#endif
/**
 * @brief reverb-minihost's counterpart of rv_log: one flushed line on stdout,
 * so the output interleaves correctly with a debugger's
 */
inline void rv_host_log(const char* fmt, ...) {
	std::va_list va;
	va_start(va, fmt);

	std::printf("[minihost] ");
	std::vprintf(fmt, va);
	std::printf("\n");
	std::fflush(stdout);

	va_end(va);
}

namespace rv::host {

/**
 * @brief How the user asked for the plugin to be loaded
 */
struct LoadOptions {
	std::string binary_path; ///< The module to load
	/// Its bundle dir (X.vst3, X.clap), or binary_path for a bare module
	std::string bundle_path;
	int class_index = 0; ///< Which class/plugin of a multi-plugin module
};

/**
 * @brief A format-specific host: runs the module entry point, creates a
 * plugin instance, and tears both down in the reverse order. No audio is
 * processed (@todo)
 */
class PluginHost {
public:
	virtual ~PluginHost() = default;
	virtual Interface format() const = 0;
	/// Runs the entry point and creates the instance
	virtual bool load(SharedLib& lib, const LoadOptions& opts) = 0;
	/// Main thread tick while the plugin is kept alive
	virtual void idle() {}
	/// Prints what the user can interact with (the info command)
	virtual void describe() const {}
	/// Undoes load(), also after a partial one
	virtual void unload() = 0;
};

/**
 * @brief Parses a case-insensitive format name
 */
std::optional<Interface> parse_interface(const std::string& name);

} // namespace rv::host
