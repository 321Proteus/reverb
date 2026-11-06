#include "core/log.hpp"
#include "decomp/analysis.hpp"
#include "factory.hpp"
#include "vst3.hpp"

namespace rv::vst3 {
void scan() {
	const ea_t gpf_ea = find_get_plugin_factory();
	if (gpf_ea == BADADDR) {
		rv_log("GetPluginFactory not found");
		return;
	}

	cfuncptr_t cf = decompile_at(gpf_ea);
	if (cf == nullptr) return;

	const auto structs = extract_factory_structs(cf);
	rv_log("Found %u factory structs in GetPluginFactory",
		unsigned(structs.size()));
}
} // namespace rv::vst3
