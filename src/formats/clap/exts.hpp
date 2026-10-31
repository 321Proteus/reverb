#pragma once

namespace rv::clap {

struct id_type_t {
	const char* name;
	const char* id;
	const char* type;
};

inline constexpr id_type_t CLAP_FACTORIES[] = {

#define RV_CLAP_FACTORY(n, id, s) { #n, id, #s },
#define RV_CLAP_EXT(n, id, p, h)

#include "exts.inc"

#undef RV_CLAP_FACTORY
#undef RV_CLAP_EXT

};

} // namespace rv::clap