#include "til.hpp"
#include "db/til/til.hpp"

namespace rv {

void apply_framework_types(
	const InterfaceSet& ifaces, const FrameworkInfo& info) {

	const FrameworkModule* mod = get_framework_module(info.fw);
	if (mod == nullptr || mod->til_name == nullptr) return;

	til::Request req{ get_framework_name(info.fw), mod->til_name,
		info.version.c_str(), ifaces.has(Interface::VST3), true };
	til::load(req);
}

} // namespace rv
