#pragma once

#include <pro.h>
namespace rv {
enum class Interface { VST2, VST3, AU, AAX, CLAP, _COUNT };

class InterfaceSet {
public:
	void add(Interface i) { value_ |= bit(i); }
	void remove(Interface i) { value_ &= ~bit(i); }
	bool has(Interface i) const { return (value_ & bit(i)) != 0; }
	bool empty() const { return value_ == 0; }

	template <class F> void for_each(F&& fn) const {
		for (int i = 0; i < int(Interface::_COUNT); i++)
			if (value_ & (1u << i)) fn(Interface(i));
	}

private:
	static constexpr uint32 bit(Interface i) { return 1u << unsigned(i); }
	uint32 value_ = 0;
};

enum class Framework { JUCE, IPLUG2, DPF, OTHER };

inline const char* get_interface_name(Interface iface) {
	switch (iface) {
	case Interface::VST2: return "VST2";
	case Interface::VST3: return "VST3";
	case Interface::AU:	  return "AU";
	case Interface::AAX:  return "AAX";
	case Interface::CLAP: return "CLAP";
	default:			  return "Other";
	}
}

inline const char* get_framework_name(Framework fw) {
	switch (fw) {
	case Framework::JUCE:	return "JUCE";
	case Framework::IPLUG2: return "iPlug2";
	case Framework::DPF:	return "Distrho Plugin Framework";
	default:				return "Other or unknown framework";
	}
}

} // namespace rv