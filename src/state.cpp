#include "state.hpp"
#include "reverb.hpp"

namespace rv {
static Interface g_iface = Interface::OTHER;
static FrameworkInfo g_framework;

Interface get_detected_interface() { return g_iface; }
void set_detected_interface(Interface iface) { g_iface = iface; }

FrameworkInfo get_detected_framework() { return g_framework; }
void set_detected_framework(FrameworkInfo info) { g_framework = info; }

} // namespace rv
