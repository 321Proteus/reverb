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
 * @brief Hooks the debugger events: while a minihost configured here runs, its
 * log is shown in the Output window
 */
void init();

/** @brief Undoes init() */
void term();

} // namespace rv::debug
