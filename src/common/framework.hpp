#pragma once
#include "reverb.hpp"

namespace rv {

typedef struct {
	const char* types_decl;
	void (*scan)();
	void (*label)();
} FrameworkModule;

const FrameworkModule* get_format_module(Interface iface);

} // namespace rv