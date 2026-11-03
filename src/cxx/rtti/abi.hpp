#pragma once

#include "db/refs.hpp"
#include <pro.h>
#include <vector>

// MSVC and Itanium RTTI, both directions. No Hex-Rays.
namespace rv::rtti {

struct base_info {
	qstring name;			 // demangled
	ea_t typeinfo = BADADDR; // Itanium typeinfo / MSVC TypeDescriptor
	sval_t offset = 0;		 // in the complete object, meaningless when virtual
	bool is_virtual = false;
	bool direct = false; // immediate base of the class
};

struct class_info {
	enum abi_t { NONE, MSVC, ITANIUM };

	qstring name;			  // demangled
	ea_t typeinfo = BADADDR;  // Itanium typeinfo / MSVC TypeDescriptor
	ea_t col = BADADDR;		  // locator the bases were read from (MSVC only)
	sval_t sub_offset = 0;	  // subobject offset (0 = primary)
	qvector<base_info> bases; // all bases, the class itself excluded
	abi_t abi = NONE;
};

/*
	RTTI of the vtable at address point vt. MSVC is tried first
   (complete object locator before the vtable), then Itanium (typeinfo
   and offset-to-top before the address point), so MinGW PEs take the
   Itanium path
*/
bool parse_rtti(ea_t vt, class_info* out);

// Class name from the vtable RTTI, or empty
qstring class_name(ea_t vt);

// Non-virtual bases at off, plus the class itself when off is 0
qvector<qstring> bases_at(const class_info& cls, sval_t off);

// Last "::" component
qstring short_name(const qstring& name);

// Typeinfo / TypeDescriptor whose name string contains str or BADADDR
ea_t find_typeinfo(const ref_index& refs, ea_t str);
/*
	Class from a typeinfo / TypeDescriptor. MSVC needs a locator to read the
   bases so a class with no vtable in this binary comes back with its name only.
*/
bool parse_typeinfo(const ref_index& refs, ea_t ti, class_info* out);

// Vtable address points (primary and secondary) of the class
std::vector<ea_t> vtables_of(const ref_index& refs, ea_t ti);

// Every typeinfo / TypeDescriptor in the database
std::vector<ea_t> collect_typeinfos(const ref_index& refs);

} // namespace rv::rtti
