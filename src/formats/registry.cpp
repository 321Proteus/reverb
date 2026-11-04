#include "format.hpp"
#include "formats/vst2/vst2.hpp"
#include "vst2_types_embed.h"
#include "formats/vst3/vst3.hpp"
#include "vst3_types_embed.h"
#include "formats/clap/clap.hpp"
#include "clap_types_embed.h"
#include "core/reverb.hpp"

namespace rv {

static const FormatModule kVst3 = { RV_VST3_TYPES_SRC, &vst3::scan,
	&vst3::label_factory };
static const FormatModule kVst2 = { RV_VST2_TYPES_SRC, &vst2::scan,
	&vst2::label_entry };
static const FormatModule kClap = { RV_CLAP_TYPES_SRC, &clap::scan,
	&clap::label };

const FormatModule* get_format_module(Interface iface) {
	switch (iface) {
	case rv::Interface::VST3: return &kVst3;
	case rv::Interface::VST2: return &kVst2;
	case rv::Interface::CLAP: return &kClap;
	default:				  return nullptr;
	}
}

} // namespace rv