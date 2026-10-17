#include "common/vtable/rtti.hpp"
#include "common/stores.hpp"
#include "util.hpp"
#include <bytes.hpp>
#include <demangle.hpp>
#include <name.hpp>
#include <nalt.hpp>

namespace rv::vtbl {

constexpr size_t MAX_NAME = 512;
constexpr uint32 MAX_BASES = 128;
constexpr int MAX_DEPTH = 16;

// Pointer-sized signed value (offset-to-top, offset_flags)
static sval_t read_sptr(ea_t ea) {
	return ptr_size() == 8 ? sval_t(int64(get_qword(ea)))
						   : sval_t(int32(get_dword(ea)));
}

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

// ---- MSVC ----

// x64 stores image-relative RVAs, x86 absolute pointers
static ea_t msvc_ptr(ea_t field, bool rva) {
	const uint32 v = get_dword(field);
	if (v == 0) return BADADDR;
	const ea_t ea = rva ? get_imagebase() + v : ea_t(v);
	return is_loaded(ea) ? ea : BADADDR;
}

// TypeDescriptor name ".?AVFoo@@" -> "Foo"
static qstring msvc_type_name(ea_t td) {
	qstring raw;
	if (td == BADADDR || !read_cstr(td + 2 * ptr_size(), raw, MAX_NAME) ||
		!raw.starts_with(".?A"))
		return qstring();

	qstring dem;
	const qstring mangled = qstring("??_R0") + raw.substr(1) + "@8";
	if (demangle_name(&dem, mangled.c_str(), MNG_SHORT_FORM) <= 0)
		return qstring();

	const size_t p = dem.find(" `RTTI");
	if (p != qstring::npos) dem.resize(p);
	if (dem.starts_with("class ")) dem.remove(0, 6);
	else if (dem.starts_with("struct "))
		dem.remove(0, 7);
	return dem;
}

static bool parse_msvc(ea_t vt, class_info* out) {
	const int psz = ptr_size();
	const ea_t col = read_ptr(vt - psz);
	if (!is_loaded(col) || !is_loaded(col + 20)) return false;

	const uint32 sig = get_dword(col);
	const bool rva = psz == 8;

	// x64: signature 1 and pSelf pointing back at the locator
	if (rva && (sig != 1 || get_imagebase() + get_dword(col + 20) != col))
		return false;
	if (!rva && sig != 0) return false;

	const ea_t td = msvc_ptr(col + 12, rva);
	const ea_t chd = msvc_ptr(col + 16, rva);
	if (td == BADADDR || chd == BADADDR) return false;

	qstring name = msvc_type_name(td);
	if (name.empty()) return false;

	out->name = name;
	out->sub_offset = sval_t(int32(get_dword(col + 4)));
	out->abi = class_info::MSVC;
	out->bases.clear();

	// Class Hierarchy Descriptor; entry 0 of the base array is the class itself
	const uint32 n = get_dword(chd + 8);
	const ea_t arr = msvc_ptr(chd + 12, rva);
	if (arr == BADADDR || n == 0 || n > MAX_BASES) return true;

	for (uint32 i = 1; i < n; i++) {
		const ea_t bcd = msvc_ptr(arr + 4 * i, rva);
		if (bcd == BADADDR) continue;

		base_info b;
		b.name = msvc_type_name(msvc_ptr(bcd, rva));
		if (b.name.empty()) continue;

		b.offset = sval_t(int32(get_dword(bcd + 8)));	 // mdisp
		b.is_virtual = int32(get_dword(bcd + 12)) != -1; // pdisp
		out->bases.push_back(b);
	}
	return true;
}

// ---- Itanium ----

// Typeinfo name "N9Steinberg14CPluginFactoryE" -> "Steinberg::CPluginFactory"
static qstring itanium_type_name(ea_t ti) {
	if (!is_loaded(ti)) return qstring();
	const ea_t p = read_ptr(ti + ptr_size());

	qstring raw;
	if (!is_loaded(p) || !read_cstr(p, raw, MAX_NAME) || raw.empty())
		return qstring();
	if (raw[0] == '*') raw.remove(0, 1);
	if (!qisdigit(raw[0]) && raw[0] != 'N' && raw[0] != 'S' && raw[0] != 'Z')
		return qstring();

	qstring dem;
	const qstring mangled = qstring("_Z") + raw;
	if (demangle_name(&dem, mangled.c_str(), MNG_SHORT_FORM) <= 0)
		return qstring();
	return dem;
}

enum ti_kind_t { TI_PLAIN, TI_SI, TI_VMI };

// vmi: flags, base_count, then { typeinfo*, long offset_flags } per base
static ea_t vmi_bases(ea_t ti) { return ti + 2 * ptr_size() + 8; }

static bool valid_vmi(ea_t ti) {
	const uint32 n = get_dword(ti + 2 * ptr_size() + 4);
	if (n == 0 || n > MAX_BASES) return false;

	for (uint32 i = 0; i < n; i++)
		if (itanium_type_name(read_ptr(vmi_bases(ti) + i * 2 * ptr_size()))
				.empty())
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
	if (!itanium_type_name(read_ptr(ti + 2 * ptr_size())).empty()) return TI_SI;
	return TI_PLAIN;
}

static void itanium_bases(
	ea_t ti, sval_t off, bool virt, int depth, qvector<base_info>& out) {
	if (depth > MAX_DEPTH || out.size() >= MAX_BASES) return;

	auto add = [&](ea_t bti, sval_t boff, bool bvirt) {
		base_info b;
		b.name = itanium_type_name(bti);
		if (b.name.empty()) return;
		b.offset = boff;
		b.is_virtual = bvirt;
		out.push_back(b);
		itanium_bases(bti, boff, bvirt, depth + 1, out);
	};

	switch (ti_kind(ti)) {
	case TI_SI:
		add(read_ptr(ti + 2 * ptr_size()), off, virt);
		break;
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
	default:
		break;
	}
}

static bool parse_itanium(ea_t vt, class_info* out) {
	const int psz = ptr_size();
	const ea_t ti = read_ptr(vt - psz);

	qstring name = itanium_type_name(ti);
	if (name.empty()) return false;

	out->name = name;
	out->sub_offset = -read_sptr(vt - 2 * psz);
	out->abi = class_info::ITANIUM;
	out->bases.clear();
	itanium_bases(ti, 0, false, 0, out->bases);
	return true;
}

bool parse_rtti(ea_t vt, class_info* out) {
	if (vt == BADADDR || !is_loaded(vt - 2 * ptr_size())) return false;

	class_info tmp;
	if (!parse_msvc(vt, &tmp) && !parse_itanium(vt, &tmp)) return false;
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

} // namespace rv::vtbl
