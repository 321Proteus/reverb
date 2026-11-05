#pragma once

#include "model.hpp"
#include <initializer_list>

namespace rv::vtbl {

struct label_policy {
	bool rename = true, set_prototypes = true, comment_shared = true;
	// __shifted(Class, off) for secondary vtables
	bool use_shifted_this = false;
};

// Changes made (except the per-kind function counts)
// clang-format off
struct label_stats {
	int vtables = 0,
		named = 0,
		prototyped = 0,
		inherited = 0,
		folded = 0,
		thunks = 0,
		purecall = 0,
		skipped = 0,
		name_taken = 0;
};
// clang-format on

/*
	Types each vtable as its def (interface _vtbl), then names and prototypes
   the slot functions (dummy/non-user-defined only)
*/
label_stats apply(const model& m, const label_policy& p = {});

// for logging
qstring format_stats(const label_stats& s);

// The first candidate vtable type that fits the extent, or empty
qstring pick_type(ea_t ea, std::initializer_list<const char*> candidates);

} // namespace rv::vtbl

namespace rv {

// Local struct type of a class, by full name or by a unique short name
tinfo_t find_class_type(const qstring& cls);

} // namespace rv
