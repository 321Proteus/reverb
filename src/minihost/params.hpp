#pragma once
#include "host.hpp"
#include <string>

/**
 * @file params.hpp
 * @brief The minihost's parameter and state commands, the same for every
 * format
 *
 * All of them call into the plugin (running from the main thread)
 */
namespace rv::host {

/**
 * @brief Logs the parameters, those whose name holds @p filter (any case)
 * when it isn't empty
 */
void list_params(const PluginHost& host, const std::string& filter);

/**
 * @brief Sets a parameter to a normalized @p value
 *
 * @param which an index, "#id", or a name (exact, else a unique part of one;
 * any case)
 */
bool set_param(PluginHost& host, const std::string& which, double value);

/** @brief Writes the plugin's state blob to @p path */
bool save_state_file(PluginHost& host, const std::string& path);

/** @brief Reads @p path and hands it to the plugin as its state */
bool load_state_file(PluginHost& host, const std::string& path);

} // namespace rv::host
