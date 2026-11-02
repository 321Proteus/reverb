#include "juce.hpp"
#include <bytes.hpp>
#include <iterator>
#include <pro.h>

namespace rv::juce {

// clang-format off
static constexpr fw_signal_t signals[] = {
	{ Framework::JUCE, "N4juce", 10 },
	{ Framework::JUCE, "@juce@@", 10 },
	{ Framework::JUCE, "JUCE v", 5 },
	{ Framework::JUCE, "juce_version_" }
};
// clang-format on

// SystemStats::getJUCEVersion() embeds "JUCE v<major>.<minor>.<build>"
static qstring detect_version() {
	ea_t ea = find_fw_string("JUCE v");

	bool post_v6 = false;
	if (ea == BADADDR) {
		ea = find_fw_string("juce_version_");
		post_v6 = true;
		if (ea == BADADDR) return {};
	}

	qstring s;
	size_t len = get_max_strlit_length(ea, STRTYPE_C, ALOPT_IGNHEADS);
	if (get_strlit_contents(&s, ea, len, STRTYPE_C) <= 0) return {};

	s.remove(0, strlen(post_v6 ? "juce_version_" : "JUCE v"));
	if (s.empty() || !qisdigit(s[0])) return {};

	if (post_v6) s.replace("_", ".");

	return s;
}

extern const FrameworkModule module = {
	nullptr, signals, std::size(signals), &detect_version, nullptr};

} // namespace rv::juce
