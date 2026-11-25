#pragma once
#include "format.hpp"
#include <ida.hpp>
#include <typeinf.hpp>

/**
 * @file types_embed.hpp
 * @brief Access to the embedded type declarations of each format, resolved for
 * the current database.
 */

// TODO: put the #ifdef RV_ENABLE_* guards back once per-format build selection
// is reintroduced
namespace rv {
/**
 * @brief Embedded type declarations of @p iface, or null if it has none
 */
inline const char* get_types_decl(Interface iface) {
	const FormatModule* fm = get_format_module(iface);
	return fm ? fm->types_decl : nullptr;
}

/**
 * @brief Drops each declaration that starts with @p marker, through its `;`
 * (it may span lines), or only the marker when the decl applies
 *
 * @param s      Declarations, modified in place
 * @param marker Placeholder starting the declarations to gate
 * @param keep   Keep the declarations, removing only the marker
 */
inline void gate_decls(qstring& s, const char* marker, bool keep) {
	if (keep) {
		s.replace(marker, "");
		return;
	}

	for (size_t p; (p = s.find(marker)) != qstring::npos;) {
		const size_t end = s.find(';', p);
		s.remove(p, (end == qstring::npos ? s.length() : end + 1) - p);
	}
}

/**
 * @brief Resolves the per-ABI placeholders of generated declarations for the
 * current database
 *
 * - `__RV_API`: the SDK's `PLUGIN_API`, `__stdcall` on 32-bit Windows
 * - `__RV_THISCALL`: C++ member functions, `__thiscall` on 32-bit Windows
 * - `__RV_ITANIUM_ONLY`: vtable slot absent on MSVC (the slot is dropped)
 * - `__RV_MAC_ONLY`: vtable slot absent off Mac (the slot is dropped)
 *
 * @param src Generated declarations
 * @return The declarations ready to parse
 */
inline qstring prepare_types_decl(const char* src) {
	qstring s(src);
	const bool pe = inf_get_filetype() == f_PE;
	const bool win32 = pe && !inf_is_64bit();
	const bool msvc = pe && inf_get_cc_id() != COMP_GNU;
	s.replace("__RV_API", win32 ? "__stdcall" : "");
	s.replace("__RV_THISCALL", win32 ? "__thiscall" : "");
	gate_decls(s, "__RV_ITANIUM_ONLY", !msvc);
	gate_decls(s, "__RV_MAC_ONLY", inf_get_filetype() == f_MACHO);
	return s;
}

} // namespace rv