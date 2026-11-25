#pragma once

/**
 * @file panel.hpp
 * @brief The REverb panel: a viewer of the report in IDA's subviews.
 */
namespace rv::panel {

/** @brief Registers the subview action and its menu entry */
void init();

/** @brief Closes the panel and removes the action */
void term();

/** @brief Opens the panel, or refreshes it and brings it to front */
void show();

/** @brief show() once the current event has returned, e.g. from an IDB callback
 */
void show_later();

/** @brief Closes the panel */
void close();

} // namespace rv::panel
