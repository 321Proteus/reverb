#pragma once

#include "uris.hpp"
#include <pro.h>
#include <vector>

namespace rv::lv2 {

// An interface returned by extension_data
struct ext_t {
	qstring uri;
	ea_t ea = BADADDR;				  // the table, or where the URI matched
	const uri_type_t* kind = nullptr; // nullptr: unknown URI
	bool conditional = false;
};

struct ext_scan_t {
	ea_t fn = BADADDR; // extension_data
	enum { NONE, NO_CHECKS, FOUND } status = NONE;
	std::vector<ext_t> exts;
};

/*
	The URIs extension_data (or the function it forwards to) compares its
	argument with, and the interface tables it returns for them
*/
ext_scan_t find_extensions(ea_t ed);

// Types, names and comments the tables and reports rows under the current
// heading
void label_extensions(const ext_scan_t& s);

} // namespace rv::lv2
