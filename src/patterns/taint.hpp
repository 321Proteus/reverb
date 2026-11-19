#pragma once

#include "core/reverb.hpp"
#include <pro.h>
#include <typeinf.hpp>
#include <vector>

namespace rv::taint {

// Strip every pointer and array level
tinfo_t innermost(tinfo_t t);

/*
	Where the audio signal buffer pointer enters the binary, declared per
   format. A function matches when its applied prototype has arg 0 pointing to
   `owner` and arg `arg` pointing (through any number of pointers) to
   `arg_type`. Names are compared after resolving typedefs, so use the struct
   name (withtout the "_t" at the end - see types.h)
*/
struct seed_spec_t {
	const char* owner;	  // pointee type of arg 0 (the plugin instance)
	int arg;			  // tainted argument index
	const char* arg_type; // innermost pointee type of the tainted arg
	bool output;		  // written buffers
	int ptrsize;		  // 4 or 8 when known, 0 when the root carries both
	const char* reason;	  // e.g. "VST2 process inputs"
};

struct seed_t {
	ea_t func = BADADDR;			   // the tainted function
	const seed_spec_t* spec = nullptr; // into a FormatModule's static table
};

// Functions whose applied prototype matches a spec of the detected formats
std::vector<seed_t> find_seeds(InterfaceSet is);

// Find the seeds (propagation follows in a later step)
void run(InterfaceSet is);

} // namespace rv::taint
