#pragma once

#include <pro.h>
namespace rv {
enum Interface { VST2, VST3, AU, AAX, CLAP, OTHER };

inline qstring get_interface_name(Interface iface) {
	switch (iface) {
	case VST2:
		return "VST2";
	case VST3:
		return "VST3";
	case AU:
		return "AU";
	case AAX:
		return "AAX";
	case CLAP:
		return "CLAP";
	case OTHER:
		return "Other";
	}

	return {};
}

} // namespace rv