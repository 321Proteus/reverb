#include "db/read.hpp"
#include "detail.hpp"
#include <demangle.hpp>
#include <name.hpp>

/*
	Itanium C++ ABI (GCC, Clang, MinGW). A typeinfo is { vptr, name* } plus,
	depending on the vptr's __cxxabiv1 class:
	  __class_type_info      nothing (no bases)
	  __si_class_type_info   base typeinfo*
	  __vmi_class_type_info  flags, base_count, { typeinfo*, offset_flags }[]
	A vtable address point has the typeinfo at -1, offset-to-top at -2.
*/
namespace rv::rtti::itanium {

// Demangled typeinfo name
static qstring type_name(ea_t ti) {
	if (!is_loaded(ti)) return qstring();
	const ea_t p = read_ptr(ti + ptr_size());

	qstring raw;
	if (!is_loaded(p) || !read_cstr(p, raw, MAX_NAME) || raw.empty())
		return qstring();

	// GCC prefixes internal-linkage types (anon namespaces) with '*'
	if (raw[0] == '*') raw.remove(0, 1);
	// Z: local classes defined inside a function
	if (raw.empty() ||
		(!qisdigit(raw[0]) && raw[0] != 'N' && raw[0] != 'S' && raw[0] != 'Z'))
		return qstring();

	qstring dem;
	const qstring mangled = qstring("_Z") + raw;
	if (demangle_name(&dem, mangled.c_str(), MNG_SHORT_FORM) <= 0)
		return qstring();
	return dem;
}

bool is_typeinfo(ea_t ti) {
	return read_ptr(ti) != 0 && !type_name(ti).empty();
}

enum ti_kind_t { TI_PLAIN, TI_SI, TI_VMI };

static ea_t vmi_bases(ea_t ti) { return ti + 2 * ptr_size() + 8; }

static bool valid_vmi(ea_t ti) {
	const uint32 n = get_dword(ti + 2 * ptr_size() + 4);
	if (n == 0 || n > MAX_BASES) return false;

	for (uint32 i = 0; i < n; i++)
		if (type_name(read_ptr(vmi_bases(ti) + i * 2 * ptr_size())).empty())
			return false;
	return true;
}

static ti_kind_t ti_kind(ea_t ti) {
	/*
		The vptr is the address point of a __cxxabiv1 vtable, two words past its
		_ZTV symbol. The address point itself is never checked: in an extern
		segment it belongs to whatever import comes next.
	*/
	const qstring n = get_name(read_ptr(ti) - 2 * ptr_size());
	if (n.find("__vmi_class_type_info") != qstring::npos) return TI_VMI;
	if (n.find("__si_class_type_info") != qstring::npos) return TI_SI;
	if (n.find("__class_type_info") != qstring::npos) return TI_PLAIN;

	if (valid_vmi(ti)) return TI_VMI;
	if (!type_name(read_ptr(ti + 2 * ptr_size())).empty()) return TI_SI;
	return TI_PLAIN;
}

static void bases(
	ea_t ti, sval_t off, bool virt, int depth, qvector<base_info>& out) {
	if (depth > MAX_DEPTH || out.size() >= MAX_BASES) return;

	auto add = [&](ea_t bti, sval_t boff, bool bvirt) {
		base_info b;
		b.name = type_name(bti);
		if (b.name.empty()) return;
		b.typeinfo = bti;
		b.offset = boff;
		b.is_virtual = bvirt;
		b.direct = depth == 0;
		out.push_back(b);
		bases(bti, boff, bvirt, depth + 1, out);
	};

	switch (ti_kind(ti)) {

	case TI_SI: {
		add(read_ptr(ti + 2 * ptr_size()), off, virt);
		break;
	}
	case TI_VMI: {
		const uint32 n = get_dword(ti + 2 * ptr_size() + 4);
		if (n > MAX_BASES) break;

		for (uint32 i = 0; i < n; i++) {
			const ea_t e = vmi_bases(ti) + i * 2 * ptr_size();
			const sval_t flags = read_sptr(e + ptr_size());
			const bool v = virt || (flags & 1) != 0;
			add(read_ptr(e), v ? 0 : off + (flags >> 8), v);
		}
		break;
	}
	default: break;
	}
}

static bool parse_ti(ea_t ti, class_info* out) {
	qstring name = type_name(ti);
	if (name.empty()) return false;

	out->name = name;
	out->typeinfo = ti;
	out->abi = class_info::ITANIUM;
	out->bases.clear();
	bases(ti, 0, false, 0, out->bases);
	return true;
}

bool parse_vtable(ea_t vt, class_info* out) {
	const int psz = ptr_size();
	if (!parse_ti(read_ptr(vt - psz), out)) return false;
	out->sub_offset = -read_sptr(vt - 2 * psz);
	return true;
}

bool parse_typeinfo(const ref_index&, ea_t ti, class_info* out) {
	class_info tmp;
	if (!parse_ti(ti, &tmp)) return false;
	if (out) *out = std::move(tmp);
	return true;
}

// The typeinfo holds a pointer to the name, right after its vptr
ea_t typeinfo_of_name(const ref_index& refs, ea_t s) {
	if (const std::vector<ea_t>* r = refs.ptr_refs(s))
		for (ea_t at : *r)
			if (is_typeinfo(at - ptr_size())) return at - ptr_size();
	return BADADDR;
}

/*
	Slot -1 is the typeinfo, slot -2 offset-to-top (0 or negative,
	pointer-aligned). Derived typeinfos also point at this one as a base;
	those fail the offset-to-top check (si: a name pointer sits there) or
	the first-slot check in abi.cpp (vmi: offset_flags sits there).
*/
void vtable_candidates(const ref_index& refs, ea_t ti, std::vector<ea_t>& out) {
	const int psz = ptr_size();
	const std::vector<ea_t>* r = refs.ptr_refs(ti);
	if (r == nullptr) return;

	for (ea_t at : *r) {
		const sval_t ott = read_sptr(at - psz);
		if (ott > 0 || ott < -MAX_OFFSET_TO_TOP || ott % psz != 0) continue;
		out.push_back(at + psz);
	}
}

// Every stored pointer whose target looks like the start of a mangled name
void collect(const ref_index& refs, std::vector<ea_t>& out) {
	for (const auto& [target, locs] : refs.ptrs()) {
		const uchar c = get_byte(target);
		if (!qisdigit(c) && c != 'N' && c != 'S' && c != 'Z' && c != '*')
			continue;

		for (ea_t at : locs)
			if (is_typeinfo(at - ptr_size())) out.push_back(at - ptr_size());
	}
}

} // namespace rv::rtti::itanium
