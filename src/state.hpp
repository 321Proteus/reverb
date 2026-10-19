#pragma once
#include "reverb.hpp"
#include "common/framework.hpp"

namespace rv {
InterfaceSet get_detected_interfaces();
void set_detected_interfaces(InterfaceSet is);

InterfaceSet get_loaded_types();
void mark_types_loaded(Interface iface);

FrameworkInfo get_detected_framework();
void set_detected_framework(FrameworkInfo info);

} // namespace rv
