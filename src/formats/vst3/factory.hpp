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

} // namespace rv::vst3
