#pragma once

#include "stores.hpp"
#include <cstring>
#include <map>
#include <vector>

// A resolved field value and the store it came from
struct field_val_t {
	val_t v;
	ea_t src_ea = BADADDR;
};

namespace rv {

enum fkind_t { F_INT, F_FN, F_PTR, F_STR, F_CSTR };

// One member of a struct layout
struct field_desc_t {
	qstring name;
	int off;
	int sz;
	fkind_t kind;
};

// Everything resolved about a constructed object
struct object_fields_t {
	int root = -1;		// lvar the object lives behind
	int64 base_off = 0; // offset of the object inside root
	std::map<qstring, field_val_t> fields;
	std::map<qstring, uint64> or_bits; // fields built up with |=
};

qstring val_str(
	const val_t& v, const object_fields_t& o, const char* self_label);

/*
	Match constant stores into o.root against a struct layout and record what
	each field was set to in o.fields. Layout-agnostic: VST2 passes the AEffect
	layout, VST3 passes whatever layout describes the factory object.
*/
void resolve_fields(const std::vector<field_desc_t>& fields, object_fields_t& o,
	const std::vector<store_t>& stores, bool is64,
	const char* self_label = "this");
} // namespace rv