#include "state.hpp"

namespace rv {
static Interface g_iface = Interface::OTHER;

Interface get_detected_interface() { return g_iface; }
void set_detected_interface(Interface iface) { g_iface = iface; }
} // namespace rv
