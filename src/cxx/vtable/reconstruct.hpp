#pragma once

#include <pro.h>

namespace rv::vtbl {

// Forces the interface of a vtable in the next reconstruction cycles, for what
// format modules know without RTTI
void pin(ea_t vt, const char* vtbl_type);

// Finds the RTTI vtables, adds the pinned ones, types them and labels
// their slots using the loaded type decls
void reconstruct();

} // namespace rv::vtbl
