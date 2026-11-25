#pragma once

#include <pro.h>

/**
 * @file clap.hpp
 * @brief CLAP format module.
 */

namespace rv::clap {

/**
 * @brief `clap_version_t` as read from the database (via @sa load_layout())
 */
struct version_t {
	uint32 major = 0, minor = 0, revision = 0;
};

/** @brief Labels what scan() found, does nothing for now as scans labels too.
 * @todo Split labeling code away from scan() to here
 */
void label();

/** @brief Finds `clap_entry` and the factories, descriptors and plugins behind
 * it
 */
void scan();
} // namespace rv::clap
