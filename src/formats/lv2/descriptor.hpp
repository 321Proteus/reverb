#pragma once

#include "extensions.hpp"
#include "instance.hpp"
#include <pro.h>
#include <typeinf.hpp>
#include <vector>

namespace rv::lv2 {

// A plugin or UI descriptor
struct desc_kind_t {
	const char* type;
	const char* label;		 // "plugin", "UI"
	const char* prefix;		 // of the callbacks <prefix>[_<id>]__<member>
	const char* data_name;	 // of the struct <data_name>_<id>
	const char* const* need; // callbacks required non-null by the spec
};

static const char* const PLUGIN_NEED[] = { "instantiate", "connect_port", "run",
	nullptr };
static const char* const UI_NEED[] = { "instantiate", nullptr };

const desc_kind_t PLUGIN = { "LV2_Descriptor", "plugin", "lv2_plugin",
	"lv2_desc", PLUGIN_NEED };
const desc_kind_t UI = { "LV2UI_Descriptor", "UI", "lv2ui", "lv2ui_desc",
	UI_NEED };

struct desc_t {
	ea_t ea = BADADDR;
	const desc_kind_t* kind = nullptr;
	qstring uri;

	// Filled in by scan
	qstrvec_t features;
	ext_scan_t exts;
	bool exts_shared = false; // extension_data of an earlier descriptor
	instance_t instance;
	bool has_instance = false; // instantiate allocates it with a known size
};

/*
	The static descriptors that getter returns (lv2_descriptor(index),
	LV2_Lib_Descriptor::get_plugin, lv2ui_descriptor(index)): referenced by
	it or the function it forwards to, alone, as an array or an array of
	pointers. Else every static descriptor in the data
*/
std::vector<desc_t> find_descriptors(ea_t getter, const desc_kind_t& kind);

/*
	Types and names the descriptor, its callbacks and extension tables, and
	adds its report section. If unique is set to true, the descriptor is treated
	as the only of its kind (callbacks are not named by URI).
	-> the name of its instance struct, empty when it has none
*/
qstring label_descriptor(const desc_t& d, bool unique);

} // namespace rv::lv2
