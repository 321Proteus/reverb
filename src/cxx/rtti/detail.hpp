#pragma once

/**
 * @file detail.hpp
 * @brief Internal to cxx/rtti/*.cpp; callers include abi.hpp instead.
 */

#include "abi.hpp"
#include "db/read.hpp"
#include <bytes.hpp>

namespace rv::rtti {

/**
 * @brief Sanity limits against corrupt or non-RTTI data
 */
constexpr size_t MAX_NAME = 512;
constexpr uint32 MAX_BASES = 128;
constexpr int MAX_DEPTH = 16;
constexpr sval_t MAX_OFFSET_TO_TOP = 0x100000;

/**
 * @brief Read a pointer-sized signed value (offset-to-top, offset_flags)
 */
inline sval_t read_sptr(ea_t ea) {
	return ptr_size() == 8 ? sval_t(int64(get_qword(ea)))
						   : sval_t(int32(get_dword(ea)));
}

/**
 * @brief Sub-namespace for ABI backends
 * @details Both ABIs (itanium.cpp, msvc.cpp) expose the same functions,
 * abi.cpp dispatches between them and holds everything ABI-independent
 *
 * - is_typeinfo:       @c ti is a typeinfo / TypeDescriptor of this ABI
 * - parse_vtable:      class of the vtable at address point @c vt, sub_offset
 * set
 * - parse_typeinfo:    class of a typeinfo, sub_offset 0
 * - typeinfo_of_name:  typeinfo naming the string starting at @c s, or BADADDR
 * - vtable_candidates: address points, before the first-slot check
 * - collect:           every typeinfo of this ABI
 */

namespace itanium {
bool is_typeinfo(ea_t ti);
bool parse_vtable(ea_t vt, class_info* out);
bool parse_typeinfo(const ref_index& refs, ea_t ti, class_info* out);
ea_t typeinfo_of_name(const ref_index& refs, ea_t s);
void vtable_candidates(const ref_index& refs, ea_t ti, std::vector<ea_t>& out);
void collect(const ref_index& refs, std::vector<ea_t>& out);
} // namespace itanium

namespace msvc {
bool is_typeinfo(ea_t ti);
bool parse_vtable(ea_t vt, class_info* out);
bool parse_typeinfo(const ref_index& refs, ea_t ti, class_info* out);
ea_t typeinfo_of_name(const ref_index& refs, ea_t s);
void vtable_candidates(const ref_index& refs, ea_t ti, std::vector<ea_t>& out);
void collect(const ref_index& refs, std::vector<ea_t>& out);
} // namespace msvc

} // namespace rv::rtti
