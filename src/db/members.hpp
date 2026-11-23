#pragma once
/**
 * @file members.hpp
 * @brief Lookup and reading of struct members by name or dotted path.
 */

#include <pro.h>
#include <typeinf.hpp>

/**
 * @brief Finds a member of @p tif by name or dotted path (`a.b.c`)
 *
 * @param tif  Struct type
 * @param path Member name or path into nested structs
 * @param off  Receives the byte offset from the start of @p tif
 * @param type Receives the member type, may be null
 * @return false if a path component is missing
 */
bool find_member(
	const tinfo_t& tif, const char* path, uint64* off, tinfo_t* type);

/**
 * @brief Pointer stored in the member @p name of the object at @p base
 *
 * @param mtype Receives the member type, may be null
 * @return The pointer, or BADADDR if the member is missing or not a pointer
 */
ea_t member_ptr(
	ea_t base, const tinfo_t& tif, const char* name, tinfo_t* mtype = nullptr);

/**
 * @brief Same as above, with the struct looked up by @p type_name in the
 * local types
 */
ea_t member_ptr(ea_t base, const char* type_name, const char* path,
	tinfo_t* mtype = nullptr);

/**
 * @brief Reads an integer member (1, 2, 4 or 8 bytes) of the object at @p base
 *
 * @return false if the member is missing or has another size
 */
bool member_value(ea_t base, const tinfo_t& tif, const char* path, uint64* out);

/**
 * @brief Address of the member @p path of the object at @p base, or BADADDR
 */
inline ea_t member_ea(ea_t base, const tinfo_t& tif, const char* path) {
	uint64 off;
	return find_member(tif, path, &off, nullptr) ? base + off : BADADDR;
}
