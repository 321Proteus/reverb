#pragma once

#include <pro.h>
#include <map>
#include <vector>
#include <set>

constexpr int MAX_DEPTH = 3;
constexpr uint32 MAX_OPCODE = 0xFF;

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

namespace rv::vst2 {

struct dispatcher_info_t {
	ea_t dispatcher = BADADDR;
	ea_t switch_func = BADADDR;
	std::set<uint32> handled;		   // opcodes with a case
	std::map<uint32, qstring> strings; // effGetEffectName etc. -> literal
	std::map<uint32, uint64> consts;   // effGetVstVersion etc. -> retval
	std::vector<qstring> can_do;	   // effCanDo strings -> canDo capabiliteis
};

bool analyze_dispatcher(ea_t dispatcher_ea, dispatcher_info_t& out);
void apply_dispatcher(const dispatcher_info_t& info);

// Opcode name from the VstOpcode enum in local types, or "opcode_N"
qstring opcode_name(uint32 op);

} // namespace rv::vst2