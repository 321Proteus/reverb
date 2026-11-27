#pragma once

/**
 * @file controls.hpp
 * @brief reverb-minihost's controls in IDA: play, pause, run blocks, info,
 * quit and its settings, in the Debugger menu and a toolbar.
 */
namespace rv::debug {

/** @brief Registers the actions, the submenu and the toolbar */
void init_controls();

/** @brief Re-evaluates the actions, e.g. when the minihost (dis)connects */
void refresh_controls();

/** @brief Undoes init_controls() */
void term_controls();

} // namespace rv::debug
