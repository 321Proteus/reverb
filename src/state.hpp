#pragma once
#include "core/reverb.hpp"
#include "frameworks/framework.hpp"

/**
 * @file state.hpp
 * @brief Utilities for managing the global analysis state: the results of
 * detection are what the currently processed binary uses
 */
namespace rv {

/**
 * @brief Get the detected interfaces object
 *
 * @return InterfaceSet
 */
InterfaceSet get_detected_interfaces();

/**
 * @brief Set the detected interfaces object
 *
 * @param is the formats the current binary uses
 */
void set_detected_interfaces(InterfaceSet is);

/**
 * @brief Get the list of frameworks for which the IDA types have been loaded,
 * regardless of whether they were made from generated headers or imported at
 * runtime from a TIL module
 *
 * @return InterfaceSet
 */
InterfaceSet get_loaded_types();

/**
 * @brief Add a framework to the list of typed ones. Typically used
 * after its types have been loaded
 *
 * @param iface The interface to add
 */
void mark_types_loaded(Interface iface);

/**
 * @brief Get the detected framework
 *
 * @return the framework used by the current binary
 */
FrameworkInfo get_detected_framework();

/**
 * @brief Set the detected framework
 *
 * @param info
 */
void set_detected_framework(FrameworkInfo info);

} // namespace rv
