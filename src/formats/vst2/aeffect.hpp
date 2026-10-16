#pragma once

#include "common/stores.hpp"
#include <hexrays.hpp>
#include <map>
#include <pro.h>
#include <vector>

// A resolved AEffect field
struct field_val_t {
	val_t v;
	ea_t src_ea = BADADDR;
};

namespace rv::vst2 {

struct aeffect_hit_t {
	ea_t func = BADADDR;
	ea_t magic_ea = BADADDR;
	int64 base_off = 0;
	int root = -1;
	std::map<qstring, field_val_t> fields;
	uint32 flags = 0;
};

enum fkind_t { F_INT, F_FN, F_PTR };

struct aeffect_field_t {
	const char* name;
	int off; // bytes from AEffect::magic
	int sz;	 // bytes
	fkind_t kind;
};

inline constexpr const char* members[] = {
	"dispatcher",
	"process",
	"setParameter",
	"getParameter",
	"numPrograms",
	"numParams",
	"numInputs",
	"numOutputs",
	"flags",
	"initialDelay",
	"ioRatio",
	"object",
	"uniqueID",
	"version",
	"processReplacing",
	"processDoubleReplacing",
};

std::vector<aeffect_hit_t> extract_aeffects(
	const std::vector<aeffect_field_t>& fields, int aeffect_size, cfunc_t* cf);

void apply_aeffect(const std::vector<aeffect_field_t>& fields,
	const aeffect_hit_t& h, bool multiple);

} // namespace rv::vst2