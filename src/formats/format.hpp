#pragma once
#include "core/reverb.hpp"
#include "patterns/taint/taint.hpp"
#include <cstddef>

namespace rv {

typedef struct {
	const char* types_decl;
	void (*scan)();
	void (*label)();
	const taint::seed_spec_t* seeds;
	size_t n_seeds;
} FormatModule;

const FormatModule* get_format_module(Interface iface);

} // namespace rv