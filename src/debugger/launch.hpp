#pragma once
#include "core/reverb.hpp"

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
 * @return true if the options now launch the minihost
 */
bool configure_minihost(InterfaceSet formats);

/**
 * @brief Listens for the minihost and hooks the debugger events: while a
 * minihost configured here runs, its log is shown in the Output window
 */
void init();

/** @brief True while a minihost is connected */
bool minihost_connected();

/**
 * @brief Sends @p command to the minihost
 *
 * @param command the command to send (e.g. info, play, blocks N, quit)
 * @return false if none is connected
 */
bool send_command(const char* command);

/** @brief Undoes init() */
void term();

} // namespace rv::debug
