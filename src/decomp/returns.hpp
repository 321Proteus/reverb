#pragma once
#include "stores.hpp"
#include <hexrays.hpp>
#include <pro.h>

/*
	Entry points that return a pointer into an object they built: VST3
	createInstance returns an interface subobject, a VST2 entry returns
	&wrapper->effect, a CLAP create_plugin &plugin->clap_plugin. With the
	right return type set, the object local still has whatever type Hex-Rays
	guessed, so obj + 0xC0 shows as an index scaled by that type
*/
namespace rv {

/*
	Where every non-null return of cf points: (object local, byte offset).
	Follows locals assigned once from an address (result = v2 + 24; return
	result;) and constant steps of the returned local (if ( result )
	++result;). Null assignments and return nullptr are the null path. False
	when the returns disagree or a returned local is assigned more than once
*/
bool return_loc(cfunc_t* cf, loc_t& out);

/*
	For fn, whose return type is already set: when it returns a local object
	plus a nonzero offset, types that local char *, so the adjustment reads
	in bytes. A local the user typed is left alone. When fn only forwards
	(return g(...);), follows up to follow direct callees. Logs under label
*/
void type_return_base(ea_t fn, const char* label, int follow = 0);

} // namespace rv
