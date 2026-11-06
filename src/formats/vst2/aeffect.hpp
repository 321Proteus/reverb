#pragma once

#include "decomp/fields.hpp"
#include <hexrays.hpp>
#include <map>
#include <pro.h>
#include <vector>

namespace rv::vst2 {

struct aeffect_hit_t : object_fields_t {
	ea_t func = BADADDR;
	ea_t magic_ea = BADADDR;
};

using aeffect_field_t = field_desc_t;

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