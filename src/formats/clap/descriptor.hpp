#pragma once

#include <pro.h>
#include <typeinf.hpp>
#include <vector>
#include "clap.hpp"

namespace rv::clap {

struct desc_t {
	ea_t ea = BADADDR;		// BADADDR when only built at runtime
	ea_t init_ea = BADADDR; // function that fills it in, if any
	abi::clap_version_t clap_version{};
	qstring id, name, vendor, version; // empty when unset (NULL or "")
	ea_t features_ea = BADADDR;
	qstrvec_t features;
};

bool read_descriptor(ea_t ea, const tinfo_t& tif, desc_t& out);

// Descriptors referenced by get_plugin_descriptor, else filled in by code
// it reaches, else every static one in data
std::vector<desc_t> find_descriptors(ea_t gpd_ea);

void label_descriptor(const desc_t& d);

} // namespace rv::clap
