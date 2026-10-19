#include "state.hpp"
#include "reverb.hpp"

namespace rv {
static InterfaceSet g_iface_set = InterfaceSet();
static FrameworkInfo g_framework;

InterfaceSet get_detected_interfaces() { return g_iface_set; }
void set_detected_interfaces(InterfaceSet is) { g_iface_set = is; }

FrameworkInfo get_detected_framework() { return g_framework; }
void set_detected_framework(FrameworkInfo info) { g_framework = info; }

} // namespace rv
