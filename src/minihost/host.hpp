#pragma once
#include "core/reverb.hpp"
#include "platform.hpp"
#include <cstdio>
#include <memory>
#include <optional>
#include <string>

/**
 * @file host.hpp
 * @brief The per-format host interface of reverb-minihost.
 */

/**
 * @brief reverb-minihost's counterpart of rv_log: one flushed line on stdout,
 * and in the log file when one is set (see rv::host::set_log_file)
 */
#if defined(__GNUC__) || defined(__clang__)
__attribute__((format(printf, 1, 2)))
#endif
void rv_host_log(const char* fmt, ...);

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
 * @brief Also writes the log to @p path (truncated first): the REverb plugin
 * follows it and shows it in IDA's Output window
 *
 * @return false if the file can't be opened
 */
bool set_log_file(const char* path);

/**
 * @brief Parses a case-insensitive format name
 */
std::optional<Interface> parse_interface(const std::string& name);

} // namespace rv::host
