#pragma once
#include "reverb.hpp"
#include "common/framework.hpp"

namespace rv {
Interface get_detected_interface();
void set_detected_interface(Interface iface);

FrameworkInfo get_detected_framework();
void set_detected_framework(FrameworkInfo info);

} // namespace rv
