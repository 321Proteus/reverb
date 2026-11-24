#pragma once

#include "decomp/fields.hpp"
#include <hexrays.hpp>
#include <pro.h>
#include <vector>

/**
 * @file factory.hpp
 * @brief Extraction of `PFactoryInfo` and `PClassInfo2` structs built by the
 * factory function.
 */
namespace rv::vst3 {

/**
 * @brief A `PFactoryInfo` or `PClassInfo2` built by the factory function
 */
struct factory_struct_t : object_fields_t {
	const char* type = nullptr; /**< Which struct it is */
};

/**
 * @brief Extracts the factory structs built by @p cf
 */
std::vector<factory_struct_t> extract_factory_structs(cfunc_t* cf);

/**
 * @brief Reads the string field @p f of @p s from the address it was set to
 *
 * @return false if the field is missing or not a string
 */
bool str_field(const factory_struct_t& s, const char* f, qstring& out);

/**
 * @brief Reads the number field @p f of @p s, with the bits set by `|=` added
 *
 * @return false if the field is missing or not a number
 */
bool num_field(const factory_struct_t& s, const char* f, uint64& out);

/**
 * @brief Builds the 16 TUID bytes from the four longs of `INLINE_UID`
 *
 * Every long is written big-endian. With `COM_COMPATIBLE` (Windows) the first
 * long and the two halves of the second are little-endian, like a GUID.
 *
 * @param l   The four longs
 * @param com Windows (`COM_COMPATIBLE`) layout
 * @param out Receives the bytes
 */
void tuid_from_longs(const uint32 (&l)[4], bool com, uchar (&out)[16]);

/**
 * @brief Matches each `PClassInfo2` in @p structs to the `registerClass` call
 * in @p cf that passes it, names its `createFunc` and pins the vtables of the
 * object it builds
 */
void label_classes(cfunc_t* cf, const std::vector<factory_struct_t>& structs);

} // namespace rv::vst3
