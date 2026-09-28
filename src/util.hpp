#pragma once
#include <pro.h>
#include <kernwin.hpp>

// Source: https://stackoverflow.com/questions/16388510
constexpr unsigned int str_cth(const char* str, int h = 0) {
	return !str[h] ? 5381 : (str_cth(str, h + 1) * 33) ^ str[h];
}

AS_PRINTF(1, 2) inline void rv_log(const char* fmt, ...) {
	qstring buf;
	va_list va;
	va_start(va, fmt);
	buf.vsprnt(fmt, va);
	va_end(va);
	msg("[REverb] %s\n", buf.c_str());
}