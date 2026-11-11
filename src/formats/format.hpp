#pragma once
#include "core/reverb.hpp"
#include "patterns/taint.hpp"
#include <cstddef>

/**
 * @file format.hpp
 * @brief Plugin format modules (VST2, VST3, CLAP) and their registry.
 */
namespace rv {

/**
 * @brief What a plugin format provides
 */
typedef struct {
	const char*
		types_decl;	 /**< Type declarations of the format's SDK, or null */
	void (*scan)();	 /**< Finds the format's entry points and records what they
						say */
	void (*label)(); /**< Names and types what scan() found */
	const taint::seed_spec_t*
		seeds;		/**< Functions introducing audio signal input or output */
	size_t n_seeds; /**< The total number of taint seeds */
} FormatModule;

/**
 * @brief The module of @p iface
 *
 * @return The module, or null if the interface has none (AU, AAX)
 */
const FormatModule* get_format_module(Interface iface);

} // namespace rv