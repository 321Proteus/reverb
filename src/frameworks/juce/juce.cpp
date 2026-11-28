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

/**
 * @brief Reads the version from `SystemStats::getJUCEVersion()`, which embeds
 * `"JUCE v<major>.<minor>.<build>"` (`juce_version_<major>_<minor>_<build>` in
 * JUCE 6 and later)
 */
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

// The AudioProcessor virtuals the plugin's own processor overrides
static const bpt_spec_t breakpoints[] = {
	{ "createPluginFilter", true },
	{ "*::prepareToPlay", true },
	{ "*::releaseResources", true },
	{ "*::createEditor", true },
	{ "*::processBlock", false },
	{ "*::processBlockBypassed", false },
	{ "*::setStateInformation", false },
	{ "*::getStateInformation", false },
	{ nullptr, false },
};

extern const FrameworkModule module = { nullptr, signals, std::size(signals),
	&detect_version, nullptr, "juce", breakpoints };

} // namespace rv::juce
