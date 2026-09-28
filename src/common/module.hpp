#pragma once
#include "reverb.hpp"

namespace rv {

typedef struct {
	const char* types_decl;
	void (*label)();
} FormatModule;

const FormatModule* get_format_module(Interface iface);

} // namespace rv