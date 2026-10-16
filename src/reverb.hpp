#pragma once

#include <pro.h>
namespace rv {
enum class Interface { VST2, VST3, AU, AAX, CLAP, OTHER };
enum class Framework { JUCE, IPLUG2, DPF, OTHER };

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

inline qstring get_framework_name(Framework fw) {
	switch (fw) {
	case Framework::JUCE:
		return "JUCE";
	case Framework::IPLUG2:
		return "iPlug2";
	case Framework::DPF:
		return "Distrho Plugin Framework";
	case Framework::OTHER:
		return "Other or unknown framework";
	}

	return {};
}

} // namespace rv