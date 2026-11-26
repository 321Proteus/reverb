#pragma once
#include "core/reverb.hpp"

/**
 * @file breakpoints.hpp
 * @brief Breakpoints on the interesting functions of a plugin, declared per
 * format and framework in their registries.
 */
namespace rv {

/**
 * @brief One kind of function to break on
 *
 * Lists end with an entry whose pattern is null.
 */
struct bpt_spec_t {
	/** Glob (`*`, `?`) over the function's name, as REverb or the binary named
	 * it, or its demangled name without arguments (`juce::Foo::process`) */
	const char* pattern;
	/** Whether it's on when added. Per-call functions (dispatchers, process
	 * callbacks) are added disabled, so that a run doesn't stop on every block
	 */
	bool enabled;
};

namespace debug {

/**
 * @brief Adds the breakpoints of @p formats and @p fw to the database
 *
 * Each module's go into the breakpoint folder `REverb/<name>`. Addresses with a
 * breakpoint already, the user's or an earlier scan's, are left as they are.
 * Call it once the scan has named the functions.
 *
 * @param formats the detected formats
 * @param fw the detected framework (OTHER for none)
 */
void add_breakpoints(InterfaceSet formats, Framework fw);

} // namespace debug
} // namespace rv
