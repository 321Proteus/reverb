#include "dpf.hpp"
#include "core/reverb.hpp"
#include <iterator>

namespace rv::dpf {

static constexpr fw_signal_t signals[] = { { Framework::DPF, "N7DISTRHO", 10 },
	{ Framework::DPF, "@DISTRHO@@", 10 },
	{ Framework::DPF, "DistrhoPluginInternal", 5 },
	{ Framework::DPF, "lv2_generate_ttl", 10 } };

extern const FrameworkModule module = { nullptr, signals, std::size(signals),
	nullptr, nullptr };

} // namespace rv::dpf
