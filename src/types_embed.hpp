#pragma once
#include "reverb.hpp"
#include "VST2_types_embed.hpp"
#include "VST3_types_embed.hpp"
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
} // namespace rv