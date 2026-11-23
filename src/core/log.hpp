#pragma once
#include <pro.h>
#include <kernwin.hpp>

/**
 * @brief A standard way of printing debug information in REverb to simplify
 * debugging
 * @param fmt The format of the output, following the standard printf syntax
 */
AS_PRINTF(1, 2) inline void rv_log(const char* fmt, ...) {
	qstring buf;
	va_list va;
	va_start(va, fmt);
	buf.vsprnt(fmt, va);
	va_end(va);
	msg("[REverb] %s\n", buf.c_str());
}
