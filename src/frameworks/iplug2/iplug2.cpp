#include "iplug2.hpp"
#include <iterator>

namespace rv::iplug2 {

static constexpr fw_signal_t signals[] = {
	{ Framework::IPLUG2, "N5iplug", 10 },
	{ Framework::IPLUG2, "@iplug@@", 10 },
};

// iplug::Plugin virtuals the plugin overrides
static const bpt_spec_t breakpoints[] = {
	{ "MakePlug", true },
	{ "*::OnReset", true },
	{ "*::OnActivate", true },
	{ "*::ProcessBlock", false },
	{ "*::OnParamChange", false },
	{ "*::SerializeState", false },
	{ "*::UnserializeState", false },
	{ nullptr, false },
};

extern const FrameworkModule module = { nullptr, signals, std::size(signals),
	nullptr, nullptr, nullptr, breakpoints };

} // namespace rv::iplug2
