#pragma once

namespace rv::panel {

// Registers the subview actions
void init();
void term();

// Opens the panel, or refreshes it and brings it to front
void show();

// show() once the current event has returned, e.g. from an IDB callback
void show_later();

// Closes the panel
void close();

} // namespace rv::panel
