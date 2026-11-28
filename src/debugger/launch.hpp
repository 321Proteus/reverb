#pragma once
#include "core/reverb.hpp"
#include <pro.h>
#include <vector>

/**
 * @file launch.hpp
 * @brief Points IDA debugger at reverb-minihost, with the detection results
 * of the database as its arguments
 */
namespace rv::debug {

/**
 * @brief Sets the debugger's process options to run reverb-minihost on the
 * input file as the first hostable format of @p formats
 *
 * Options pointing at another application are the user's and stay as they
 * are; ones that are unset, the input file itself (IDA's default) or an
 * earlier minihost are replaced.
 *
 * @param formats the formats detected in the database
 * @param keep_typed whether a remote minihost path typed into Process options
 * is kept (and saved in the settings) rather than replaced; not when the
 * settings were just edited, where the settings' path is the user's choice
 * @return true if the options now launch the minihost
 */
bool configure_minihost(InterfaceSet formats, bool keep = false);

/**
 * @brief configure_minihost() again with the formats it last saw, e.g. after
 * the settings changed
 */
bool reconfigure_minihost(bool keep = false);

/** @brief The hostable formats configure_minihost() last saw, best first */
std::vector<Interface> hostable_formats();

/**
 * @brief Listens for the minihost and hooks the debugger events: while a
 * minihost configured here runs, its log is shown in the Output window (see
 * channel.hpp)
 */
void init();

/** @brief Undoes init() */
void term();

} // namespace rv::debug
