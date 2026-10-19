#pragma once
#include "reverb.hpp"
#include "vst2_types_embed.h"
#include "vst3_types_embed.h"
#include <ida.hpp>
// #include "AAX_types_embed.hpp"
// #include "CLAP_types_embed.hpp"

// TODO: put the #ifdef RV_ENABLE_* guards back once per-format build
// selection is reintroduced - right now everything is always compiled in.

namespace rv {
inline const char* get_types_decl(Interface iface) {
	switch (iface) {
	case Interface::VST2:
		return RV_VST2_TYPES_SRC;
	case Interface::VST3:
		return RV_VST3_TYPES_SRC;
	case Interface::AAX:
		// return RV_AAX_TYPES_SRC;
	case Interface::CLAP:
		// return RV_CLAP_TYPES_SRC;
	default:
		return nullptr;
	}
}

// Generated decls spell the SDK's PLUGIN_API as __RV_API: __stdcall on
// 32-bit Windows, nothing elsewhere. Resolve it for the current database.
inline qstring prepare_types_decl(const char* src) {
	qstring s(src);
	const bool stdcall = !inf_is_64bit() && inf_get_filetype() == f_PE;
	s.replace("__RV_API", stdcall ? "__stdcall" : "");
	return s;
}

} // namespace rv