#pragma once
#include "core/reverb.hpp"
#include "debugger/breakpoint.hpp"

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
	/** Functions to break on, by the names label() gives or null */
	const bpt_spec_t* breakpoints;
} FormatModule;

/**
 * @brief The module of @p iface
 *
 * @return The module, or null if the interface has none (AU, AAX)
 */
const FormatModule* get_format_module(Interface iface);

} // namespace rv