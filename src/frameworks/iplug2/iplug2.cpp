#include "iplug2.hpp"
#include <iterator>

namespace rv::iplug2 {

static constexpr fw_signal_t signals[] = {
	{Framework::IPLUG2, "N5iplug", 10},
	{Framework::IPLUG2, "@iplug@@", 10},
};

extern const FrameworkModule module = {
	nullptr, signals, std::size(signals), nullptr, nullptr};

} // namespace rv::iplug2
