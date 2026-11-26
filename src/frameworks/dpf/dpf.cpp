#include "dpf.hpp"
#include <iterator>

namespace rv::dpf {

static constexpr fw_signal_t signals[] = {
	{ Framework::DPF, "N7DISTRHO", 10 },
	{ Framework::DPF, "@DISTRHO@@", 10 },
	{ Framework::DPF, "DistrhoPluginInternal", 5 },
};

// DISTRHO::Plugin virtuals + of DPF VST2 wrapper callbacks
static const bpt_spec_t breakpoints[] = {
	{ "*::createPlugin", true },
	{ "*::activate", true },
	{ "*::deactivate", true },
	{ "*::sampleRateChanged", true },
	{ "*::run", false },
	{ "*::initParameter", false },
	{ "*::setParameterValue", false },
	{ "*::loadProgram", false },
	{ "*::setState", false },
	{ "*vst_dispatcherCallback", false },
	{ "*vst_processReplacingCallback", false },
	{ nullptr, false },
};

extern const FrameworkModule module = { nullptr, signals, std::size(signals),
	nullptr, nullptr, nullptr, breakpoints };

} // namespace rv::dpf
