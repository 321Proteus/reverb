#pragma once

#include <pro.h>

namespace rv::clap {

// clap_version_t as read from the database (via load_layout)
struct version_t {
	uint32 major = 0, minor = 0, revision = 0;
};

void label();
void scan();
} // namespace rv::clap
