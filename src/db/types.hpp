#pragma once
/**
 * @file types.hpp
 * @brief Applying types at addresses and reading struct layouts from local
 * types.
 */
#include "core/log.hpp"
#include <pro.h>
#include <bytes.hpp>
#include "db/members.hpp"
#include "decomp/fields.hpp"
#include <cstring>
#include <typeinf.hpp>
#include <vector>

/**
 * @brief Applies the local type @p type_name at @p ea, replacing the items
 * there
 *
 * @param out Receives the type, may be null
 * @return false if the type is missing, unsized or cannot be applied
 */
inline bool apply_type_at(
	ea_t ea, const char* type_name, tinfo_t* out = nullptr) {
	tinfo_t tif;
	if (!tif.get_named_type(nullptr, type_name)) {
		rv_log("Error: type %s not loaded", type_name);
		return false;
	}
	const size_t sz = tif.get_size();
	if (sz == BADSIZE) return false;

	del_items(ea, DELIT_SIMPLE, sz);

	if (!apply_tinfo(ea, tif, TINFO_DEFINITE)) {
		rv_log("apply_tinfo(%s) failed at %a", type_name, ea);
		return false;
	}
	if (out != nullptr) *out = tif;
	return true;
}

namespace rv {

/**
 * @brief A member layout from the local types
 */
struct layout_t {
	std::vector<field_desc_t>
		fields;	  /**< Members, nested structs flattened by path */
	int size = 0; /**< Size of the struct in bytes, 0 if unknown */

	/** @return The field named @p name (possibly a dotted path), or null */
	const field_desc_t* find(const char* name) const {
		for (const field_desc_t& f : fields)
			if (f.name == name) return &f;
		return nullptr;
	}
};

/**
 * @brief Kind of a member from its type: function pointer, C string pointer,
 * other pointer, array, or integer
 */
inline fkind_t field_kind(const tinfo_t& t) {
	if (t.is_funcptr()) return F_FN;

	if (t.is_ptr()) {
		const tinfo_t c = t.get_pointed_object();
		return c.is_char() || c.is_uchar() ? F_CSTR : F_PTR;
	}

	return t.is_array() ? F_STR : F_INT;
}

/**
 * @brief Reads the named members of a local struct type into @p out
 *
 * A member may be a path into nested structs (e.g. `version.major`). Kinds
 * follow the member type:
 * - `F_FN`: function pointer
 * - `F_CSTR`: pointer to string
 * - `F_PTR`: regular pointer
 * - `F_STR`: array
 * - `F_INT`: anything else
 *
 * @param type          Struct name in the local types
 * @param members       Member names or paths
 * @param n             Number of @p members
 * @param out           Receives the layout
 * @param expected_size Required struct size in bytes, 0 to skip the check
 * @param require_all   A missing member fails the load; otherwise it is skipped
 * @return false on failure or if no field was loaded
 */
inline bool load_layout(const char* type, const char* const* members, size_t n,
	layout_t& out, int expected_size = 0, bool require_all = true) {
	tinfo_t t;
	if (!t.get_named_type(get_idati(), type) || !t.is_struct()) {
		rv_log("%s not found in local types - was the type declaration parsed?",
			type);
		return false;
	}

	const size_t sz = t.get_size();
	if (expected_size > 0 && (sz == BADSIZE || int(sz) != expected_size)) {
		rv_log("%s is %d bytes, expected %d - check types.h declarations", type,
			sz == BADSIZE ? -1 : int(sz), expected_size);
		return false;
	}
	out.size = sz == BADSIZE ? 0 : int(sz);

	for (size_t i = 0; i < n; i++) {
		uint64 off;
		tinfo_t mt;
		const size_t msz =
			find_member(t, members[i], &off, &mt) ? mt.get_size() : BADSIZE;
		if (msz == BADSIZE) {
			rv_log("%s::%s missing from types.h%s", type, members[i],
				require_all ? "" : ", skipping");
			if (require_all) return false;
			continue;
		}

		out.fields.push_back(
			{ members[i], int(off), int(msz), field_kind(mt) });
	}
	return !out.fields.empty();
}

/**
 * @brief load_layout() for a static array of member names
 */
template <size_t N>
bool load_layout(const char* type, const char* const (&members)[N],
	layout_t& out, int expected_size = 0, bool require_all = true) {
	return load_layout(type, members, N, out, expected_size, require_all);
}

/**
 * @brief Appends the members of @p t at @p base to @p out, nested structs
 * flattened into dotted paths
 */
inline void flatten_members(
	const tinfo_t& t, const qstring& prefix, int64 base, layout_t& out) {
	udt_type_data_t udt;
	if (!t.get_udt_details(&udt)) return;

	for (const udm_t& m : udt) {
		if (m.is_gap() || m.is_bitfield()) continue;

		const qstring path = prefix.empty() ? m.name : prefix + "." + m.name;
		const int64 off = base + int64(m.offset / 8);

		if (m.type.is_struct()) {
			flatten_members(m.type, path, off, out);
			continue;
		}

		out.fields.push_back(
			{ path, int(off), int(m.size / 8), field_kind(m.type) });
	}
}

/**
 * @brief Reads every member of a local struct type
 *
 * The layout follows the type declaration with no member list to keep in
 * sync. Nested structs are flattened into dotted paths.
 */
inline bool load_layout(const char* type, layout_t& out) {
	tinfo_t t;
	if (!t.get_named_type(get_idati(), type) || !t.is_struct()) {
		rv_log("%s not found in local types - was the type declaration parsed?",
			type);
		return false;
	}

	const size_t sz = t.get_size();
	out.size = sz == BADSIZE ? 0 : int(sz);

	flatten_members(t, qstring(), 0, out);
	return !out.fields.empty();
}

/**
 * @brief Finds a member of type @p target inside @p ti, searching nested
 * structs
 *
 * @param base  Offset of @p ti in the outer struct
 * @param off   Receives the byte offset of the first match
 * @return false if there is none (search depth is limited)
 */
inline bool find_embedded(const tinfo_t& ti, const tinfo_t& target, int64 base,
	int64& off, int depth = 0) {
	udt_type_data_t u;
	if (depth > 8 || !ti.get_udt_details(&u)) return false;

	for (const udm_t& m : u) {
		int64 k = base + int64(m.offset / 8);

		tinfo_t mt = m.type;
		mt.clr_const();

		if (mt.equals_to(target)) {
			off = k;
			return true;
		}

		if (mt.is_struct()) {
			if (find_embedded(mt, target, k, off, depth + 1)) return true;
		}
	}
	return false;
}

} // namespace rv
