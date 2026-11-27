#include "detect.hpp"
#include "core/reverb.hpp"
#include <string>
#include <vector>

static const char* lstrip(const char* s) { return (s[0] == '_') ? s + 1 : s; }

rv::InterfaceSet classify_from_exports(
	const std::vector<std::string>& exports) {

	rv::InterfaceSet found;

	// clang-format off
	for (const std::string& ex : exports) {
		switch (str_cth(lstrip(ex.c_str()))) {

		case str_cth("VSTPluginMain"):
			found.add(rv::Interface::VST2);
			break;
		case str_cth("GetPluginFactory"):
			found.add(rv::Interface::VST3);
			break;
		case str_cth("ACFRegisterComponent"):
			found.add(rv::Interface::AAX);
			break;
		case str_cth("clap_entry"):
			found.add(rv::Interface::CLAP);
			break;
		case str_cth("AUPluginInstanceEntry"):
			found.add(rv::Interface::AU);
			break;

		}
	}
	// clang-format on

	return found;
}