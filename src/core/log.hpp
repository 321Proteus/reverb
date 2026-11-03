#pragma once
#include <pro.h>
#include <kernwin.hpp>

AS_PRINTF(1, 2) inline void rv_log(const char* fmt, ...) {
	qstring buf;
	va_list va;
	va_start(va, fmt);
	buf.vsprnt(fmt, va);
	va_end(va);
	msg("[REverb] %s\n", buf.c_str());
}
