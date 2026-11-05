#pragma once
#include "format.hpp"
#include <ida.hpp>
#include <typeinf.hpp>

// TODO: put the #ifdef RV_ENABLE_* guards back once per-format build selection
// is reintroduced
namespace rv {
inline const char* get_types_decl(Interface iface) {
	const FormatModule* fm = get_format_module(iface);
	return fm ? fm->types_decl : nullptr;
}

/*
	Generated decls have placeholders for per-ABI differences.
	Resolve them for the current database:

	__RV_API           the SDK's PLUGIN_API: __stdcall on 32-bit Windows
	__RV_THISCALL      C++ member functions: __thiscall on 32-bit Windows
	__RV_ITANIUM_ONLY  vtable slot absent on MSVC (comments the line out)
	__RV_MAC_ONLY      vtable slot absent off Mac (comments the line out)
*/
inline qstring prepare_types_decl(const char* src) {
	qstring s(src);
	const bool pe = inf_get_filetype() == f_PE;
	const bool win32 = pe && !inf_is_64bit();
	const bool msvc = pe && inf_get_cc_id() != COMP_GNU;
	s.replace("__RV_API", win32 ? "__stdcall" : "");
	s.replace("__RV_THISCALL", win32 ? "__thiscall" : "");
	s.replace("__RV_ITANIUM_ONLY", msvc ? "//" : "");
	s.replace("__RV_MAC_ONLY", inf_get_filetype() == f_MACHO ? "" : "//");
	return s;
}

} // namespace rv