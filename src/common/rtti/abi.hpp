#pragma once

#include <pro.h>

namespace rv::rtti {

struct base_info {
	qstring name;	   // demangled
	sval_t offset = 0; // in the complete object, meaningless when virtual
	bool is_virtual = false;
};

struct class_info {
	enum abi_t { NONE, MSVC, ITANIUM };

	qstring name;			  // demangled
	sval_t sub_offset = 0;	  // offset of this vtable's subobject (0 = primary)
	qvector<base_info> bases; // all bases, the class itself excluded
	abi_t abi = NONE;
};

/*
	RTTI of the vtable at address point vt. MSVC is tried first (complete
	object locator before the vtable), then Itanium (typeinfo and
	offset-to-top before the address point), so MinGW PEs take the Itanium
	path
*/
bool parse_rtti(ea_t vt, class_info* out);

// Non-virtual bases at off, plus the class itself when off is 0
qvector<qstring> bases_at(const class_info& cls, sval_t off);

// Last "::" component
qstring short_name(const qstring& name);

// Class name from the vtable RTTI, or empty
qstring class_name(ea_t vt);

} // namespace rv::rtti
