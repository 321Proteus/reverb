#pragma once
#include "core/log.hpp"
#include <pro.h>
#include <bytes.hpp>
#include <typeinf.hpp>

inline bool apply_type_at(
	ea_t ea, const char* type_name, tinfo_t* out = nullptr) {
	tinfo_t tif;
	if (!tif.get_named_type(nullptr, type_name)) {
		rv_log("Error: type %s not loaded", type_name);
		return false;
	}
	const size_t sz = tif.get_size();
	if (sz == BADSIZE) return false;

	del_items(ea, DELIT_SIMPLE, sz);

	if (!apply_tinfo(ea, tif, TINFO_DEFINITE)) {
		rv_log("apply_tinfo(%s) failed at %a", type_name, ea);
		return false;
	}
	if (out != nullptr) *out = tif;
	return true;
}
