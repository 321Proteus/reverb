#pragma once

#include "decomp/fields.hpp"
#include <hexrays.hpp>
#include <pro.h>
#include <vector>

namespace rv::vst3 {

// Root of a global object. Its stores keep the absolute address in loc_t::off,
// so base_off + field offset still matches them
inline constexpr int GLOBAL_ROOT = -1;

// A PFactoryInfo or PClassInfo2 built by the factory function
struct factory_struct_t : object_fields_t {
	const char* type = nullptr;
};

std::vector<factory_struct_t> extract_factory_structs(cfunc_t* cf);

// INLINE_UID every long is written big-endian. With COM_COMPATIBLE (Windows)
// the first long and the two halves of the second are LE, like a guid.
void tuid_from_longs(const uint32 (&l)[4], bool com, uchar (&out)[16]);

/*
	Matches each PClassInfo2 in structs to the registerClass call in cf that
   passes it, names its createFunc and pins the vtables of the object build
*/
void label_classes(cfunc_t* cf, const std::vector<factory_struct_t>& structs);

} // namespace rv::vst3
