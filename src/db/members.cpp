#include "members.hpp"
#include "core/log.hpp"
#include "read.hpp"

bool find_member(
	const tinfo_t& tif, const char* path, uint64* off, tinfo_t* type) {
	tinfo_t cur = tif;
	uint64 o = 0;

	for (const char* p = path; *p != '\0';) {
		const char* dot = strchr(p, '.');

		udm_t udm;
		udm.name = dot ? qstring(p, dot - p) : qstring(p);
		if (cur.find_udm(&udm, STRMEM_NAME) < 0) return false;

		o += udm.offset / 8;
		cur = udm.type;
		if (!dot) break;

		p = dot + 1;
	}

	*off = o;
	if (type != nullptr) *type = cur;

	return true;
}

ea_t member_ptr(
	ea_t base, const tinfo_t& tif, const char* path, tinfo_t* mtype) {
	uint64 off;
	tinfo_t t;

	if (!find_member(tif, path, &off, &t) || !t.is_ptr()) return BADADDR;
	if (mtype != nullptr) *mtype = t;

	return read_ptr(base + off);
}

ea_t member_ptr(
	ea_t base, const char* type_name, const char* path, tinfo_t* mtype) {

	tinfo_t tif;

	if (!tif.get_named_type(nullptr, type_name)) {
		rv_log("Error type %s not found", type_name);
		return BADADDR;
	}

	return member_ptr(base, tif, path, mtype);
}

bool member_value(
	ea_t base, const tinfo_t& tif, const char* path, uint64* out) {
	uint64 off;
	tinfo_t t;

	if (!find_member(tif, path, &off, &t)) return false;

	const ea_t ea = base + off;
	switch (t.get_size()) {
	case 1:	 *out = get_byte(ea); return true;
	case 2:	 *out = get_word(ea); return true;
	case 4:	 *out = get_dword(ea); return true;
	case 8:	 *out = get_qword(ea); return true;
	default: return false;
	}
}