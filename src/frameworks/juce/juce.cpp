#include "juce.hpp"
#include "util.hpp"
#include <bytes.hpp>
#include <iterator>

namespace rv::juce {

static constexpr fw_signal_t signals[] = { { Framework::JUCE, "N4juce", 10 },
	{ Framework::JUCE, "@juce@@", 10 }, { Framework::JUCE, "JUCE v", 5 },
	{ Framework::JUCE, "juce_version_", 5 } };

// SystemStats::getJUCEVersion() embeds "JUCE v<major>.<minor>.<build>"
static qstring detect_version() {
	ea_t ea = find_fw_string(signals[2].pattern);
	bool post_v6 = false;

	if (ea == BADADDR) {
		post_v6 = true;
		ea = find_fw_string(signals[3].pattern);
		if (ea == BADADDR) return {};
	}

	qstring s;
	size_t len = get_max_strlit_length(ea, STRTYPE_C, ALOPT_IGNHEADS);
	if (get_strlit_contents(&s, ea, len, STRTYPE_C) <= 0) return {};

	s.remove(0, strlen(signals[post_v6 ? 3 : 2].pattern));
	if (s.empty() || !qisdigit(s[0])) return {};

	return s;
}

extern const FrameworkModule module = { nullptr, signals, std::size(signals),
	&detect_version, nullptr };

} // namespace rv::juce
