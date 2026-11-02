#pragma once

#include <bytes.hpp>
#include <funcs.hpp>
#include <pro.h>
#include "exts.hpp"

namespace rv::clap {

struct factory_hit_t {
	ea_t ea = BADADDR;
	const id_type_t* kind = nullptr;
	bool assumed = false;
};

bool is_fn_table(ea_t ea, const tinfo_t& tif);

std::vector<factory_hit_t> find_factories(ea_t gf_ea);

} // namespace rv::clap