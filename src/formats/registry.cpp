/**
 * @file registry.cpp
 * @brief Table of the available format modules.
 */
#include "format.hpp"
#include "formats/vst2/vst2.hpp"
#include "vst2_types_embed.h"
#include "formats/vst3/vst3.hpp"
#include "vst3_types_embed.h"
#include "formats/clap/clap.hpp"
#include "clap_types_embed.h"
#include "core/reverb.hpp"

namespace rv {

// clang-format off
/*
	AEffect callbacks (process, processReplacing, processDoubleReplacing):
	void f(AEffect *, float **inputs, float **outputs, VstInt32 sampleFrames)
	AudioEffect(X) virtuals of the same names take this in place of AEffect *.
	processVariableIo(AudioEffectX *, VstVariableIo *) carries both buffers
*/

static const taint::seed_spec_t kVst2Seeds[] = {
	{ "AEffect",      1, "float",         false, 4, "VST2 process inputs" },
	{ "AEffect",      2, "float",         true,  4, "VST2 process outputs" },
	{ "AEffect",      1, "double",        false, 8, "VST2 processDoubleReplacing inputs" },
	{ "AEffect",      2, "double",        true,  8, "VST2 processDoubleReplacing outputs" },
	{ "AudioEffect",  1, "float",         false, 4, "AudioEffect::process inputs" },
	{ "AudioEffect",  2, "float",         true,  4, "AudioEffect::process outputs" },
	{ "AudioEffect",  1, "double",        false, 8, "AudioEffect::processDoubleReplacing inputs" },
	{ "AudioEffect",  2, "double",        true,  8, "AudioEffect::processDoubleReplacing outputs" },
	{ "AudioEffectX", 1, "float",         false, 4, "AudioEffectX::process inputs" },
	{ "AudioEffectX", 2, "float",         true,  4, "AudioEffectX::process outputs" },
	{ "AudioEffectX", 1, "double",        false, 8, "AudioEffectX::processDoubleReplacing inputs" },
	{ "AudioEffectX", 2, "double",        true,  8, "AudioEffectX::processDoubleReplacing outputs" },
	{ "AudioEffectX", 1, "VstVariableIo", false, 4, "AudioEffectX::processVariableIo" },
};

// IAudioProcessor::process(this, ProcessData *data)
static const taint::seed_spec_t kVst3Seeds[] = {
	{ "IAudioProcessor", 1, "ProcessData", false, 0, "VST3 IAudioProcessor::process" },
};

// clap_plugin::process(const clap_plugin_t *, const clap_process_t *)
static const taint::seed_spec_t kClapSeeds[] = {
	{ "clap_plugin", 1, "clap_process", false, 0, "CLAP process" },
};
// clang-format on

static const FormatModule kVst3 = { RV_VST3_TYPES_SRC, &vst3::scan,
	&vst3::label_factory, kVst3Seeds, qnumber(kVst3Seeds) };
static const FormatModule kVst2 = { RV_VST2_TYPES_SRC, &vst2::scan,
	&vst2::label_entry, kVst2Seeds, qnumber(kVst2Seeds) };
static const FormatModule kClap = { RV_CLAP_TYPES_SRC, &clap::scan,
	&clap::label, kClapSeeds, qnumber(kClapSeeds) };

const FormatModule* get_format_module(Interface iface) {
	switch (iface) {
	case rv::Interface::VST3: return &kVst3;
	case rv::Interface::VST2: return &kVst2;
	case rv::Interface::CLAP: return &kClap;
	default:				  return nullptr;
	}
}

} // namespace rv