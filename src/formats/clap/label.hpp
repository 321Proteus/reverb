#pragma once

#include <pro.h>
#include <typeinf.hpp>

namespace rv::clap {

struct fn_ref_t {
	ea_t ea = BADADDR;
	bool keep_name = false;
	bool shared = false;
};

// Referenced from anywhere other than [owner, owner + owner_size)?
fn_ref_t classify_fn(ea_t fn, ea_t owner, size_t owner_size);

/*
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

} // namespace rv::clap
