#pragma once

#include <pro.h>
namespace rv {
enum class Interface { VST2, VST3, AU, AAX, CLAP, OTHER };

inline qstring get_interface_name(Interface iface) {
	switch (iface) {
	case Interface::VST2:
		return "VST2";
	case Interface::VST3:
		return "VST3";
	case Interface::AU:
		return "AU";
	case Interface::AAX:
		return "AAX";
	case Interface::CLAP:
		return "CLAP";
	case Interface::OTHER:
		return "Other";
	}

	return {};
}

} // namespace rv