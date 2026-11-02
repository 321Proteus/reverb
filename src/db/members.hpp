#pragma once

#include <pro.h>
#include <typeinf.hpp>

bool find_member(
	const tinfo_t& tif, const char* path, uint64* off, tinfo_t* type);

ea_t member_ptr(
	ea_t base, const tinfo_t& tif, const char* name, tinfo_t* mtype = nullptr);

ea_t member_ptr(ea_t base, const char* type_name, const char* path,
	tinfo_t* mtype = nullptr);

bool member_value(ea_t base, const tinfo_t& tif, const char* path, uint64* out);

inline ea_t member_ea(ea_t base, const tinfo_t& tif, const char* path) {
	uint64 off;
	return find_member(tif, path, &off, nullptr) ? base + off : BADADDR;
}
