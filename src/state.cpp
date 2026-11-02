#include "state.hpp"
#include "core/reverb.hpp"

namespace rv {
static InterfaceSet g_iface_set = InterfaceSet();
static InterfaceSet g_loaded_types = InterfaceSet();
static FrameworkInfo g_framework;

InterfaceSet get_detected_interfaces() { return g_iface_set; }
void set_detected_interfaces(InterfaceSet is) { g_iface_set = is; }

InterfaceSet get_loaded_types() { return g_loaded_types; }
void mark_types_loaded(Interface iface) { g_loaded_types.add(iface); }

FrameworkInfo get_detected_framework() { return g_framework; }
void set_detected_framework(FrameworkInfo info) { g_framework = info; }

} // namespace rv
