#include "module.hpp"
#include "VST2/vst2.hpp"
#include "VST3/vst3.hpp"
#include "VST3_types_embed.hpp"
#include "VST2_types_embed.hpp"
#include "reverb.hpp"
#include "util.hpp"

namespace rv {

static const FormatModule kVst3 = {RV_VST3_TYPES_SRC, &vst3::label_factory};
static const FormatModule kVst2 = {RV_VST2_TYPES_SRC, &vst2::label_entry};

const FormatModule* get_format_module(Interface iface) {
	switch (iface) {
	case rv::Interface::VST3:
		return &kVst3;
	case rv::Interface::VST2:
		return &kVst2;
	default:
		return nullptr;
	}
}

} // namespace rv