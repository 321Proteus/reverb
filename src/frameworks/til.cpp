#include "til.hpp"
#include "core/log.hpp"
#include "db/til/pick.hpp"
#include "db/til/til.hpp"

namespace rv {

// The one version that crashes the IDA parser somehow
static bool is_juce_7_0_3(const FrameworkInfo& info) {
	til::Version v;
	return info.fw == Framework::JUCE &&
		   til::parse_version(info.version.c_str(), &v) && v.major == 7 &&
		   v.minor == 0 && v.patch == 3;
}

void apply_framework_types(
	const InterfaceSet& ifaces, const FrameworkInfo& info) {

	const FrameworkModule* mod = get_framework_module(info.fw);
	if (mod == nullptr || mod->til_name == nullptr) return;

	const char* version = info.version.c_str();

	if (is_juce_7_0_3(info) && ifaces.has(Interface::VST3)) {
		rv_log("%s types: the type library for %s holds only VST2 types, using "
			   "version 7.0.2",
			get_framework_name(info.fw), version);
		version = "7.0.2";
	}

	til::Request req{ get_framework_name(info.fw), mod->til_name, version,
		true };
	til::load(req);
}

} // namespace rv
