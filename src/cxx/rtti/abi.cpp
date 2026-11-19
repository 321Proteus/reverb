#include "db/segments.hpp"
#include "db/read.hpp"
#include "detail.hpp"
#include <algorithm>
#include <pro.h>

/**
 * @file abi.cpp
 * @brief ABI-neutral helpers and dispatch between itanium.cpp and msvc.cpp.
 */
namespace rv::rtti {

qstring short_name(const qstring& name) {
	int depth = 0;
	size_t start = 0;
	for (size_t i = 0; i < name.length(); i++) {
		const char c = name[i];
		if (c == '<') depth++;
		else if (c == '>')
			depth--;
		else if (c == ':' && depth == 0 && i + 1 < name.length() &&
				 name[i + 1] == ':') {
			start = i + 2;
			i++;
		}
	}
	return name.substr(start);
}

qstring short_name(const tinfo_t& t) {
	qstring n;
	if (!t.get_final_type_name(&n)) t.get_type_name(&n);
	return short_name(n);
}

bool parse_rtti(ea_t vt, class_info* out) {
	if (vt == BADADDR || !is_loaded(vt - 2 * ptr_size())) return false;

	class_info tmp;
	if (!msvc::parse_vtable(vt, &tmp) && !itanium::parse_vtable(vt, &tmp))
		return false;
	if (out) *out = std::move(tmp);
	return true;
}

qvector<qstring> bases_at(const class_info& cls, sval_t off) {
	qvector<qstring> out;
	if (off == 0 && !cls.name.empty()) out.push_back(cls.name);

	for (const base_info& b : cls.bases)
		if (!b.is_virtual && b.offset == off) out.add_unique(b.name);
	return out;
}

qstring class_name(ea_t vt) {
	class_info ci;
	return parse_rtti(vt, &ci) ? ci.name : qstring();
}

/**
 * @brief Start of the NUL-terminated string containing @p ea, or BADADDR
 */
static ea_t string_start(ea_t ea) {
	for (size_t i = 0; i < MAX_NAME; i++, ea--)
		if (!is_loaded(ea - 1) || get_byte(ea - 1) == 0) return ea;
	return BADADDR;
}

ea_t find_typeinfo(const ref_index& refs, ea_t str) {
	const ea_t s = string_start(str);
	if (s == BADADDR) return BADADDR;

	// MSVC names start with ".?A", Itanium ones never with '.'
	return get_byte(s) == '.' ? msvc::typeinfo_of_name(refs, s)
							  : itanium::typeinfo_of_name(refs, s);
}

bool parse_typeinfo(const ref_index& refs, ea_t ti, class_info* out) {
	return msvc::is_typeinfo(ti) ? msvc::parse_typeinfo(refs, ti, out)
								 : itanium::parse_typeinfo(refs, ti, out);
}

/**
 * @brief Plausible first vtable slot: code, or an import (pure virtual stubs
 * and external overrides land in the extern segment)
 */
static bool slot_like(ea_t target) {
	if (target == 0 || target == BADADDR) return false;
	const segment_t* s = getseg(target);
	return is_code_addr(target) || (s != nullptr && s->type == SEG_XTRN);
}

std::vector<ea_t> vtables_of(const ref_index& refs, ea_t ti) {
	std::vector<ea_t> cand;
	if (msvc::is_typeinfo(ti)) msvc::vtable_candidates(refs, ti, cand);
	else
		itanium::vtable_candidates(refs, ti, cand);

	std::vector<ea_t> out;
	for (ea_t vt : cand)
		if (slot_like(read_ptr(vt)) &&
			std::find(out.begin(), out.end(), vt) == out.end())
			out.push_back(vt);
	return out;
}

std::vector<ea_t> collect_typeinfos(const ref_index& refs) {
	std::vector<ea_t> out;
	itanium::collect(refs, out);
	msvc::collect(refs, out);
	return out;
}

} // namespace rv::rtti
