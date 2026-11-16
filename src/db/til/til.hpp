#pragma once
#include <pro.h>

namespace rv::til {

struct Request {
	const char* label;
	const char* name; // the files: rv_<name>[_v<version>]_<abi>.til
	const char* version = nullptr; // nullptr if unversioned
	bool vst3 = false;
	bool import_types = false; // also copy every type into the local types
};

/*
	Add the library to the database's type libraries and, if asked, copy its
   types into the local types. True if a library was added
*/
bool load(const Request& req);

/*
	Move the local types with select ordinals into a folder of the
   Local Types view
*/
size_t group_local_types(uint32 first, uint32 last, const char* folder);

} // namespace rv::til
