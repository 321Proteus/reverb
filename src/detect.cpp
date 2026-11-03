#include "detect.hpp"
#include "core/reverb.hpp"
#include <hexrays.hpp>
#include <pro.h>
#include <vector>
#include <typeinf.hpp>

// Source: https://stackoverflow.com/questions/16388510
static constexpr unsigned int str_cth(const char* str, int h = 0) {
	return !str[h] ? 5381 : (str_cth(str, h + 1) * 33) ^ str[h];
}

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