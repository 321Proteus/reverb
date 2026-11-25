#pragma once

#include <pro.h>
#include <typeinf.hpp>
#include <vector>
#include "clap.hpp"

/**
 * @file descriptor.hpp
 * @brief Plugin descriptors (`clap_plugin_descriptor_t`) read from the
 * database.
 */

namespace rv::clap {

/**
 * @brief A plugin descriptor
 */
struct desc_t {
	ea_t ea = BADADDR;		/**< BADADDR when only built at runtime */
	ea_t init_ea = BADADDR; /**< Function that fills it in, if any */
	version_t clap_version{};
	qstring id, name, vendor, version; /**< Empty when unset (NULL or `""`) */
	ea_t features_ea = BADADDR;		   /**< Address of the features array */
	qstrvec_t features;				   /**< Feature strings */
};

/**
 * @brief Reads the descriptor of type @p tif at @p ea
 *
 * @return false if it does not look like a descriptor
 */
bool read_descriptor(ea_t ea, const tinfo_t& tif, desc_t& out);

/**
 * @brief Finds the plugin descriptors
 *
 * Descriptors referenced by `get_plugin_descriptor` (@p gpd_ea), else filled
 * in by code it reaches, else every static one in data.
 */
std::vector<desc_t> find_descriptors(ea_t gpd_ea);

/**
 * @brief Types and names the descriptor @p d and the strings it references
 */
void label_descriptor(const desc_t& d);

} // namespace rv::clap
