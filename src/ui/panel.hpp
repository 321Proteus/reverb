#pragma once

namespace rv::panel {

// Registers the subview actions
void init();
void term();

// Opens panel or brings it to front
void show();

// Closes the panel
void close();

} // namespace rv::panel
