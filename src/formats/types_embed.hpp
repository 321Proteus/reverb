#pragma once
#include "format.hpp"
#include <ida.hpp>

// TODO: put the #ifdef RV_ENABLE_* guards back once per-format build selection
// is reintroduced
namespace rv {
inline const char* get_types_decl(Interface iface) {
	const FormatModule* fm = get_format_module(iface);
	return fm ? fm->types_decl : nullptr;
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