#include "db/segments.hpp"
#include "db/read.hpp"
#include "detail.hpp"
#include <demangle.hpp>
#include <name.hpp>
#include <nalt.hpp>

/*
	MSVC RTTI. vtable[-1] -> Complete Object Locator -> TypeDescriptor (holds
	the ".?AV..." name inline) and Class Hierarchy Descriptor -> base array
	of Base Class Descriptors. x64 stores the locator's fields as 32-bit
	image-relative RVAs; vtable[-1] itself is always an absolute pointer.
*/
namespace rv::rtti::msvc {

static bool use_rva() { return ptr_size() == 8; }

// x64 stores image-relative RVAs, x86 absolute pointers
static ea_t field_ptr(ea_t field, bool rva) {
	const uint32 v = get_dword(field);
	if (v == 0) return BADADDR;
	const ea_t ea = rva ? get_imagebase() + v : ea_t(v);
	return is_loaded(ea) ? ea : BADADDR;
}

// Demangled TypeDescriptor name
static qstring type_name(ea_t td) {
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

bool is_typeinfo(ea_t ti) { return !type_name(ti).empty(); }

/*
	Complete Object Locator: signature, offset, cdOffset, pTypeDescriptor,
	pClassHierarchyDescriptor, and on x64 pSelf (all fields 4 bytes).
	x64 is validated by pSelf; x86 has no pSelf, so the hierarchy descriptor
	is checked instead (it is what reverse lookups land on falsely, e.g. a
	Base Class Descriptor that also points at the TypeDescriptor).
*/
static bool valid_col(ea_t col) {
	const bool rva = use_rva();
	if (!is_loaded(col) || !is_loaded(col + (rva ? 20 : 16))) return false;

	const uint32 sig = get_dword(col);
	if (rva) return sig == 1 && get_imagebase() + get_dword(col + 20) == col;
	if (sig != 0) return false;

	const ea_t chd = field_ptr(col + 16, false);
	if (chd == BADADDR || get_dword(chd) != 0) return false; // CHD signature
	const uint32 n = get_dword(chd + 8);
	return n != 0 && n <= MAX_BASES;
}

static bool parse_col(ea_t col, class_info* out) {
	if (!valid_col(col)) return false;

	const bool rva = use_rva();
	const ea_t td = field_ptr(col + 12, rva);
	const ea_t chd = field_ptr(col + 16, rva);
	if (td == BADADDR || chd == BADADDR) return false;

	qstring name = type_name(td);
	if (name.empty()) return false;

	out->name = name;
	out->typeinfo = td;
	out->col = col;
	out->sub_offset = sval_t(int32(get_dword(col + 4)));
	out->abi = class_info::MSVC;
	out->bases.clear();

	/*
		Class Hierarchy Descriptor entry 0 of the base array is the class
		itself. The array is a pre-order flattening of the base tree: each
		Base Class Descriptor stores numContainedBases at +4, so the direct
		bases are found by skipping each entry subtree
	*/
	const uint32 n = get_dword(chd + 8);
	const ea_t arr = field_ptr(chd + 12, rva);
	if (arr == BADADDR || n == 0 || n > MAX_BASES) return true;

	uint32 next_direct = 1;
	for (uint32 i = 1; i < n; i++) {
		const ea_t bcd = field_ptr(arr + 4 * i, rva);
		const bool direct = i == next_direct;

		if (bcd == BADADDR) {
			if (direct) next_direct = i + 1; // subtree size unknown
			continue;
		}
		if (direct) next_direct = i + 1 + get_dword(bcd + 4);

		base_info b;
		b.typeinfo = field_ptr(bcd, rva);
		b.name = type_name(b.typeinfo);
		if (b.name.empty()) continue;

		b.direct = direct;
		b.offset = sval_t(int32(get_dword(bcd + 8)));	 // mdisp
		b.is_virtual = int32(get_dword(bcd + 12)) != -1; // pdisp
		out->bases.push_back(b);
	}
	return true;
}

bool parse_vtable(ea_t vt, class_info* out) {
	return parse_col(read_ptr(vt - ptr_size()), out);
}

// Locators describing the TypeDescriptor td, one per vtable of the class
static std::vector<ea_t> cols_of(const ref_index& refs, ea_t td) {
	std::vector<ea_t> out;
	const std::vector<ea_t>* r =
		use_rva() ? refs.rva_refs(td) : refs.ptr_refs(td);
	if (r == nullptr) return out;

	for (ea_t at : *r) // pTypeDescriptor is at +12
		if (valid_col(at - 12)) out.push_back(at - 12);
	return out;
}

bool parse_typeinfo(const ref_index& refs, ea_t ti, class_info* out) {
	ea_t pick = BADADDR;
	for (ea_t col : cols_of(refs, ti)) {
		if (pick == BADADDR) pick = col;
		if (get_dword(col + 4) == 0) {
			pick = col;
			break;
		}
	}

	class_info tmp;
	if (pick != BADADDR) {
		if (!parse_col(pick, &tmp)) return false;
	} else {
		// No vtable in this binary?
		tmp.name = type_name(ti);
		if (tmp.name.empty()) return false;
		tmp.typeinfo = ti;
		tmp.abi = class_info::MSVC;
	}

	if (out) *out = std::move(tmp);
	return true;
}

// The name is inline in the TypeDescriptor, after pVFTable and spare
ea_t typeinfo_of_name(const ref_index&, ea_t s) {
	const ea_t td = s - 2 * ptr_size();
	return is_typeinfo(td) ? td : BADADDR;
}

// TypeDescriptor <- locator <- vtable[-1]
void vtable_candidates(const ref_index& refs, ea_t ti, std::vector<ea_t>& out) {
	for (ea_t col : cols_of(refs, ti))
		if (const std::vector<ea_t>* r = refs.ptr_refs(col))
			for (ea_t at : *r)
				out.push_back(at + ptr_size());
}

// TypeDescriptors are { pVFTable, spare, ".?AV..." / ".?AU..." }
void collect(const ref_index&, std::vector<ea_t>& out) {
	const int psz = ptr_size();
	const int flags = BIN_SEARCH_FORWARD | BIN_SEARCH_CASE |
					  BIN_SEARCH_NOBREAK | BIN_SEARCH_NOSHOW;

	for (const char* lit : { "\".?AV\"", "\".?AU\"" }) {
		compiled_binpat_vec_t bp;
		qstring err;
		if (!parse_binpat_str(
				&bp, inf_get_min_ea(), lit, 16, PBSENC_DEF1BPU, &err))
			continue;

		for (segment_t* s = get_first_seg(); s != nullptr;
			s = get_next_seg(s->start_ea)) {
			if (!is_data_seg(s)) continue;

			for (ea_t ea = s->start_ea; ea < s->end_ea; ea++) {
				ea = bin_search(ea, s->end_ea, bp, flags);
				if (ea == BADADDR) break;

				const ea_t td = ea - 2 * psz;
				if (td % psz == 0 && is_typeinfo(td)) out.push_back(td);
			}
		}
	}
}

} // namespace rv::rtti::msvc
