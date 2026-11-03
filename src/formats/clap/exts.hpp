#pragma once

#include <pro.h>
#include <cstring>

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

// Plugin extensions; type is "NONE" for host-only ones
inline constexpr id_type_t CLAP_EXTS[] = {

#define RV_CLAP_FACTORY(n, id, s)
#define RV_CLAP_EXT(n, id, p, h) { #n, id, #p },

#include "exts.inc"

#undef RV_CLAP_FACTORY
#undef RV_CLAP_EXT

};

template <size_t N>
const id_type_t* by_id(const id_type_t (&table)[N], const char* id) {
	for (const id_type_t& k : table)
		if (strcmp(k.id, id) == 0) return &k;
	return nullptr;
}

// A _COMPAT id and its stable id name the same struct, prefer the stable one
inline const id_type_t* prefer(const id_type_t* a, const id_type_t* b) {
	const bool a_compat = qstring(a->name).ends_with("_COMPAT");
	const bool b_compat = qstring(b->name).ends_with("_COMPAT");
	return a_compat && !b_compat ? b : a;
}

} // namespace rv::clap