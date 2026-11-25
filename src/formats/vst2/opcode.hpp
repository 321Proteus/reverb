#pragma once

#include <map>
#include <pro.h>
#include <set>
#include <vector>
#include <set>

/**
 * @file opcode.hpp
 * @brief Analysis of the VST2 dispatcher: which opcodes it handles and what it
 * returns for them.
 */

/** @cond INTERNAL */
constexpr int MAX_DEPTH = 4;
constexpr int MAX_HARVEST_DEPTH = 2;
constexpr int MAX_ENTRY_DEPTH = 2;
constexpr uint32 MAX_OPCODE = 0xFF;

constexpr int ARG_OPC = 1;
constexpr int ARG_PTR = 4;

constexpr uint32 EFF_GET_PLUG_CATEGORY = 35;
constexpr uint32 EFF_CAN_DO = 51;
constexpr uint32 EFF_GET_VST_VERSION = 58;

constexpr struct str_op_t {
	uint32 op;
	size_t max_len;
} STRING_OPS[] = {
	{ 45, 32 }, // effGetEffectName
	{ 47, 64 }, // effGetVendorString
	{ 48, 64 }	// effGetProductString
};

constexpr uint32 CONST_OPS[] = {
	35, // effGetPlugCategory
	49, // effGetVendorVersion
	58	// effGetVstVersion
};

inline const char* category_name(uint64 c) {
	static const char* const cat_names[] = { "unknown", "effect", "synth",
		"analysis", "mastering", "spacializer", "room fx", "surround fx",
		"restoration", "offline", "shell", "generator" };
	return c < qnumber(cat_names) ? cat_names[c] : "???";
}

/** @endcond */

namespace rv::vst2 {

/**
 * @brief What the dispatcher does with the opcodes
 */
struct dispatcher_info_t {
	ea_t dispatcher = BADADDR;	/**< `AEffect::dispatcher` */
	ea_t switch_func = BADADDR; /**< First function with a real opcode switch */
	ea_t vtable = BADADDR; /**< Plugin object vtable used for virtual calls */
	qstring class_name;	   /**< From RTTI, if present */
	std::set<uint32> handled; /**< Opcodes with a dedicated case */
	std::map<uint32, qstring>
		strings; /**< `effGetEffectName` etc. -> literal */
	std::map<uint32, uint64>
		consts;					 /**< `effGetVstVersion` etc. -> return value */
	std::vector<qstring> can_do; /**< Strings seen in `effCanDo` */
};

/**
 * @brief Most derived vtable of the plugin object, found by following the
 * `audioMaster` argument from the VST2 entry point to the constructor
 *
 * @param entry_ea VST2 entry point
 * @return The vtable address point, or BADADDR
 */
ea_t find_plugin_vtable(ea_t entry_ea);

/**
 * @brief Walks the dispatcher, following direct and virtual forwarding calls
 *
 * @param dispatcher_ea Dispatcher function
 * @param vtable        Plugin object vtable, or BADADDR
 * @param object_off    Byte offset of `AEffect::object` (-1 if unknown)
 * @param out           Receives the result
 * @return false if the dispatcher cannot be analyzed
 */
bool analyze_dispatcher(
	ea_t dispatcher_ea, ea_t vtable, int64 object_off, dispatcher_info_t& out);

/**
 * @brief Writes the findings as a comment on the dispatcher: object class,
 * opcode strings and constants, `effCanDo` strings
 *
 * @param infos One entry per dispatcher result, several when a shell plugin
 * shares it
 */
void apply_dispatcher(const std::vector<dispatcher_info_t>& infos);

/**
 * @brief Opcode name from the `VstOpcode` enum in local types, or `opcode_N`
 */
qstring opcode_name(uint32 op);

} // namespace rv::vst2