#pragma once

/**
 * @file abi.hpp
 * @brief ABI-neutral RTTI parsing for MSVC and Itanium (GCC/Clang/MinGW).
 *
 * Works in both directions: vtable -> class, and name/typeinfo -> class and
 * vtables.
 */

#include "db/refs.hpp"
#include <pro.h>
#include <vector>

namespace rv::rtti {

/**
 * @brief One base class of a parsed class
 */
struct base_info {
	qstring name;			 /**< Demangled name */
	ea_t typeinfo = BADADDR; /**< Itanium typeinfo / MSVC TypeDescriptor */
	sval_t offset =
		0; /**< Offset in the complete object. Meaningless if virtual */
	bool is_virtual = false; /**< Virtual base */
	bool direct = false;	 /**< Immediate base of the class */
};

/**
 * @brief RTTI of a single class (or of one of its vtables)
 */
struct class_info {
	enum abi_t { NONE, MSVC, ITANIUM };

	qstring name;			 /**< Demangled name */
	ea_t typeinfo = BADADDR; /**< Itanium typeinfo / MSVC TypeDescriptor */
	ea_t col = BADADDR;	   /**< Locator the bases were read from (MSVC only) */
	sval_t sub_offset = 0; /**< Subobject offset of the vtable (0 = primary) */
	qvector<base_info> bases; /**< All bases, the class itself excluded */
	abi_t abi = NONE;
};

/**
 * @brief Parse the RTTI of the vtable whose address point is @p vt
 *
 * MSVC is tried first (complete object locator before the vtable), then
 * Itanium (typeinfo and offset-to-top before the address point)
 *
 * @param vt  Vtable address point
 * @param out Receives the result, may be null to only test
 * @return true if valid RTTI was found
 */
bool parse_rtti(ea_t vt, class_info* out);

/**
 * @brief Class name from the vtable RTTI, or empty
 */
qstring class_name(ea_t vt);

/**
 * @brief Non-virtual bases at offset @p off, plus the class itself when @p off
 * is 0
 */
qvector<qstring> bases_at(const class_info& cls, sval_t off);

/**
 * @brief The last top-level `::` component (template arguments are skipped)
 */
qstring short_name(const qstring& name);

/**
 * @brief Find the typeinfo / TypeDescriptor whose name string contains @p str.
 * @return Its address, or BADADDR
 */
ea_t find_typeinfo(const ref_index& refs, ea_t str);

/**
 * @brief Parse a class from a typeinfo / TypeDescriptor
 *
 * MSVC needs a locator to read the bases, so a class with no vtable in this
 * binary comes back with its name only
 */
bool parse_typeinfo(const ref_index& refs, ea_t ti, class_info* out);

/**
 * @brief Vtable address points (primary and secondary) of the class
 */
std::vector<ea_t> vtables_of(const ref_index& refs, ea_t ti);

/**
 * @brief Every typeinfo / TypeDescriptor in the database
 */
std::vector<ea_t> collect_typeinfos(const ref_index& refs);

} // namespace rv::rtti
