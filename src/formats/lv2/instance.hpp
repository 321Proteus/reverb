#pragma once

#include <pro.h>

namespace rv::lv2 {

// The object instantiate allocates and returns as the LV2_Handle
struct instance_t {
	ea_t inst = BADADDR;	   // instantiate function
	int var = -1;			   // its returned local, at scan time
	size_t size = 0;		   // bytes allocated
	ea_t site = BADADDR;	   // the allocation
	ea_t alloc = BADADDR;	   // the allocator called there
	ea_t ctor = BADADDR;	   // the first call that gets the object, if any
	bool shared_local = false; // var holds other values too: left untyped
};

/*
	inst returns a local that it assigned the result of an allocator called
	with a constant size (operator new, malloc, or a small function that
	calls malloc, as a statically linked operator new). False otherwise
*/
bool find_instance(ea_t inst, instance_t& out);

/*
	Creates the fixed-layout struct type_name of the allocated size (an
	existing one is reused), types the returned local and the constructor's
	first argument as a pointer to it. Then adds the feature pointers stored
	into it as members (urid_map...). Adds report rows
*/
void label_instance(const instance_t& in, const char* type_name);

} // namespace rv::lv2
