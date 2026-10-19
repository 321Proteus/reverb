#include "detect.hpp"
#include "reverb.hpp"
#include <hexrays.hpp>
#include <pro.h>
#include <vector>
#include "util.hpp"
#include <typeinf.hpp>

static const char* lstrip(const char* s) { return (s[0] == '_') ? s + 1 : s; }

rv::InterfaceSet classify_from_exports(const std::vector<qstring>& exports) {

	rv::InterfaceSet found;

	for (const qstring& ex : exports) {
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

	return found;
}