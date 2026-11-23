#pragma once

#include "core/reverb.hpp"
namespace rv {

struct fw_signal_t {
	Framework fw;
	const char* pattern;
	int weight;
};

struct FrameworkModule {
	const char* types_decl;
	const fw_signal_t* signals;
	int num_signals;

	qstring (*detect_version)();
	void (*label)(Interface iface);

	// "juce": the framework's type libraries are rv_juce_v<version>_<abi>.til
	const char* til_name = nullptr;
};

struct FrameworkInfo {
	Framework fw = Framework::OTHER;
	qstring version;
	int score = 0;
};

FrameworkInfo scan_framework();
const FrameworkModule* get_framework_module(Framework fw);

// First hit of a literal in the searchable segments, BADADDR if none
ea_t find_fw_string(const char* lit);

} // namespace rv