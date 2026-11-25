#pragma once

/**
 * @file fields.hpp
 * @brief Resolving the members of constructed objects against a struct layout.
 */

#include "stores.hpp"
#include <map>
#include <vector>

/**
 * @brief A resolved field value and the store it came from
 */
struct field_val_t {
	val_t v;
	ea_t src_ea = BADADDR;
};

namespace rv {

/**
 * @brief What a field holds: integer, function pointer, pointer, array or C
 * string pointer
 */
enum fkind_t { F_INT, F_FN, F_PTR, F_STR, F_CSTR };

/**
 * @brief One member of a struct layout
 */
struct field_desc_t {
	qstring name; /**< Member name or dotted path */
	int off;	  /**< Byte offset in the struct */
	int sz;		  /**< Size in bytes */
	fkind_t kind;
};

/**
 * @brief Everything resolved about a constructed object
 */
struct object_fields_t {
	int root = -1;		/**< lvar the object lives behind */
	int64 base_off = 0; /**< Offset of the object inside root */
	std::map<qstring, field_val_t> fields; /**< Value of each field */
	std::map<qstring, uint64> or_bits;	   /**< Fields built up with `|=` */
};

/**
 * @brief Display text of a value; @p self_label names the object itself
 */
qstring val_str(
	const val_t& v, const object_fields_t& o, const char* self_label);

/**
 * @brief Matches constant stores into `o.root` against a struct layout and
 * records what each field was set to in `o.fields`
 *
 * Layout-agnostic: VST2 passes the AEffect layout, VST3 passes whatever layout
 * describes the factory object.
 */
void resolve_fields(const std::vector<field_desc_t>& fields, object_fields_t& o,
	const std::vector<store_t>& stores, bool is64,
	const char* self_label = "this");

/**
 * @brief resolve_fields() for an object initialized at @p ea
 *
 * Each member is read from the IDB as if it was stored. `o.root` and
 * `o.base_off` are overwritten.
 */
void resolve_data_fields(const std::vector<field_desc_t>& fields, ea_t ea,
	object_fields_t& o, bool is64, const char* self_label = "this");

} // namespace rv