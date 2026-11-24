#pragma once

#include <pro.h>
#include <typeinf.hpp>

namespace rv {

struct fn_ref_t {
	ea_t ea = BADADDR;
	bool keep_name = false;
	bool shared = false;
};

// Referenced from anywhere other than [owner, owner + owner_size)?
fn_ref_t classify_fn(ea_t fn, ea_t owner, size_t owner_size);

/*
	Function tables of the C formats (CLAP, LV2): structs of callbacks.
	Prototypes and names fn as prefix__member. Shared and already-named
	functions are commented and skipped. owner is what references fn (the
	struct, or the function that stores the pointer). A callback that only
	forwards to another function also names that one prefix__member_impl
*/
void label_fn(
	ea_t fn, const udm_t& m, const char* prefix, ea_t owner, size_t owner_size);

// Types the struct and labels every function member
void label_fn_table(ea_t ea, const char* type, const char* prefix);

// Non-alphanumerics become _
qstring c_ident(const qstring& s);

/*
	Whether every func pointer member points into local code, other pointer
	members don't, and the struct is loaded outside GOT. Runs of GOT slots
	(strcmp, local functions in PIC) and vtables are not tables. With
	allow_null, NULL callbacks (optional members, as in LV2) are accepted,
	but at least one must be set
*/
bool is_fn_table(ea_t ea, const tinfo_t& tif, bool allow_null = false);

// is_fn_table at ea, or one pointer hop away (GOT slot). Updates ea
bool resolve_table(ea_t& ea, const tinfo_t& tif, bool allow_null = false);

} // namespace rv
