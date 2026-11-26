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

/**
 * Breakpoints, by the names the rxports and label() give.
 * Lifecycle functions are on, per-call ones (dispatch, processing, state) are
 * added off.
 */
static const bpt_spec_t kVst3Bpts[] = {
	{ "*InitDll", true },
	{ "*ModuleEntry", true },
	{ "*bundleEntry", true },
	{ "*GetPluginFactory", true },
	{ "vst3_createInstance_*", true },
	{ "vst3_registerClass", false },
	{ "*::initialize", true },
	{ "*::terminate", true },
	{ "*::setupProcessing", true },
	{ "*::setActive", true },
	{ "*::setProcessing", false },
	{ "*::process", false },
	{ "*::setState", false },
	{ "*::getState", false },
	{ "*::createView", false },
	{ nullptr, false },
};

// vst2_<AEffect field>, _<uniqueID> in shell plugins, _impl behind wrappers
static const bpt_spec_t kVst2Bpts[] = {
	{ "*VSTPluginMain", true },
	{ "*main_macho", true },
	{ "main", true },
	{ "_main", true },
	{ "vst2_dispatcher*", false },
	{ "vst2_processReplacing*", false },
	{ "vst2_processDoubleReplacing*", false },
	{ "vst2_process*", false },
	{ "vst2_setParameter*", false },
	{ "vst2_getParameter*", false },
	{ nullptr, false },
};

// <table>__<member>; instances are clap_plugin, or clap_plugin_<id> when the
// module has several
static const bpt_spec_t kClapBpts[] = {
	{ "clap_entry__init", true },
	{ "clap_entry__deinit", true },
	{ "clap_entry__get_factory", false },
	{ "clap_plugin_factory__create_plugin", true },
	{ "clap_plugin*__init", true },
	{ "clap_plugin*__destroy", true },
	{ "clap_plugin*__activate", true },
	{ "clap_plugin*__deactivate", true },
	{ "clap_plugin*__start_processing", true },
	{ "clap_plugin*__stop_processing", true },
	{ "clap_plugin*__process", false },
	{ "clap_plugin*__get_extension", false },
	{ "clap_plugin*__on_main_thread", false },
	{ "clap_plugin_state__*", false },
	{ "clap_plugin_params__*", false },
	{ nullptr, false },
};

static const FormatModule kVst3 = { RV_VST3_TYPES_SRC, &vst3::scan,
	&vst3::label_factory, kVst3Bpts };
static const FormatModule kVst2 = { RV_VST2_TYPES_SRC, &vst2::scan,
	&vst2::label_entry, kVst2Bpts };
static const FormatModule kClap = { RV_CLAP_TYPES_SRC, &clap::scan,
	&clap::label, kClapBpts };

/**
 * @brief Table of format modules for scanning, labelling, and type
 * access
 */
const FormatModule* get_format_module(Interface iface) {
	switch (iface) {
	case rv::Interface::VST3: return &kVst3;
	case rv::Interface::VST2: return &kVst2;
	case rv::Interface::CLAP: return &kClap;
	default:				  return nullptr;
	}
}

} // namespace rv