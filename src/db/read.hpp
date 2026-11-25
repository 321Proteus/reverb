#pragma once
/**
 * @file read.hpp
 * @brief Small database readers: pointers, strings, four-character codes.
 */
#include <pro.h>
#include <bytes.hpp>
#include <ida.hpp>

/** @brief Pointer size of the database, 4 or 8 */
inline int ptr_size() { return inf_is_64bit() ? 8 : 4; }

/** @brief Pointer-sized database load */
inline ea_t read_ptr(ea_t ea) {
	return ptr_size() == 8 ? ea_t(get_qword(ea)) : ea_t(get_dword(ea));
}

/**
 * @brief Whether @p s is non-empty printable ASCII or UTF-8 without control
 * characters
 */
inline bool is_text(const qstring& s) {
	const size_t len = s.length();

	for (size_t i = 0; i < len;) {

		const uchar c = uchar(s[i]);
		size_t n; // continuation bytes

		if (c < 0x80) {
			if (c < 0x20 || c == 0x7F) return false;
			n = 0;
		} else if ((c & 0xE0) == 0xC0)
			n = 1;
		else if ((c & 0xF0) == 0xE0)
			n = 2;
		else if ((c & 0xF8) == 0xF0)
			n = 3;
		else
			return false;

		if (i + n >= len) return false;

		for (size_t k = 1; k <= n; k++)
			if ((uchar(s[i + k]) & 0xC0) != 0x80) return false;

		i += n + 1;
	}

	return len != 0;
}

/**
 * @brief Reads a C string at @p ea through IDA's string reader
 *
 * @param max_len Longest string accepted when @p ea is not yet a string item
 * @return false unless the result is non-empty text, @sa is_text()
 */
inline bool read_cstr(ea_t ea, qstring& out, size_t max_len = 256) {
	if (!is_strlit(get_flags(ea))) {
		size_t n = get_max_strlit_length(ea, STRTYPE_C, ALOPT_IGNHEADS);
		if (n < 2 || n > max_len) return false;
	}
	if (get_strlit_contents(&out, ea, -1, STRTYPE_C) <= 0) return false;
	return is_text(out);
}

/**
 * @brief Reads a raw NUL-terminated UTF-8 string without IDA's string reader
 * (which gets some emoji wrong)
 *
 * An empty string is valid.
 */
inline bool read_utf8(ea_t ea, qstring& out, size_t max_len = 1024) {
	out.clear();
	for (size_t i = 0; i < max_len; i++) {
		if (!is_loaded(ea + i)) return false;

		const uchar c = get_byte(ea + i);
		if (c == 0) return out.empty() || is_text(out);
		out += char(c);
	}
	return false;
}

/**
 * @brief Formats a four-character code, e.g. `'VST3' (0x56535433)`
 *
 * Non-printable bytes are shown as `.`.
 */
inline qstring fourcc(uint32 v) {
	char c[5] = { char(v >> 24), char(v >> 16), char(v >> 8), char(v), 0 };
	for (int i = 0; i < 4; i++)
		if (uchar(c[i]) < 0x20 || uchar(c[i]) >= 0x7F) c[i] = '.';
	qstring s;
	s.sprnt("'%s' (0x%08X)", c, v);
	return s;
}
