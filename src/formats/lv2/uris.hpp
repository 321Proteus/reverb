#pragma once

#include <cstring>

/*
	The URIs of the LV2 SDK (lv2/include/lv2) that REverb knows the C types
	of, one table per direction:
	- extensions: interfaces the plugin gives the host, returned by
	  extension_data(); type is the interface struct
	- features: what the host gives the plugin, in instantiate()'s features
	  array, type is what LV2_Feature::data points to, nullptr for a
	  feature without data
*/
namespace rv::lv2 {

struct uri_type_t {
	const char* uri;
	const char* type;
};

#define RV_LV2 "http://lv2plug.in/ns/"

// Returned by LV2_Descriptor::extension_data and LV2UI_Descriptor's
inline constexpr uri_type_t EXTENSIONS[] = {
	{ RV_LV2 "ext/options#interface", "LV2_Options_Interface" },
	{ RV_LV2 "ext/state#interface", "LV2_State_Interface" },
	{ RV_LV2 "ext/worker#interface", "LV2_Worker_Interface" },
	{ RV_LV2 "extensions/ui#idleInterface", "LV2UI_Idle_Interface" },
	{ RV_LV2 "extensions/ui#showInterface", "LV2UI_Show_Interface" },
	{ RV_LV2 "extensions/ui#resize", "LV2UI_Resize" },
};

// LV2_Feature::URI -> the type LV2_Feature::data points to
inline constexpr uri_type_t FEATURES[] = {
	{ RV_LV2 "ext/urid#map", "LV2_URID_Map" },
	{ RV_LV2 "ext/urid#unmap", "LV2_URID_Unmap" },
	{ RV_LV2 "ext/log#log", "LV2_Log_Log" },
	{ RV_LV2 "ext/worker#schedule", "LV2_Worker_Schedule" },
	{ RV_LV2 "ext/options#options", "LV2_Options_Option" }, // array
	{ RV_LV2 "ext/state#mapPath", "LV2_State_Map_Path" },
	{ RV_LV2 "ext/state#makePath", "LV2_State_Make_Path" },
	{ RV_LV2 "ext/state#freePath", "LV2_State_Free_Path" },
	{ RV_LV2 "ext/resize-port#resize", "LV2_Resize_Port_Resize" },
	{ RV_LV2 "ext/data-access", "LV2_Extension_Data_Feature" },
	{ RV_LV2 "ext/instance-access", "LV2_Handle" },
	{ RV_LV2 "ext/uri-map", "LV2_URI_Map_Feature" }, // deprecated
	{ RV_LV2 "ext/event", "LV2_Event_Feature" },	 // deprecated
	{ RV_LV2 "extensions/ui#parent", "LV2UI_Widget" },
	{ RV_LV2 "extensions/ui#portMap", "LV2UI_Port_Map" },
	{ RV_LV2 "extensions/ui#portSubscribe", "LV2UI_Port_Subscribe" },
	{ RV_LV2 "extensions/ui#touch", "LV2UI_Touch" },
	{ RV_LV2 "extensions/ui#requestValue", "LV2UI_Request_Value" },
	{ RV_LV2 "extensions/ui#resize", "LV2UI_Resize" },
	{ RV_LV2 "ext/buf-size#boundedBlockLength", nullptr },
	{ RV_LV2 "ext/buf-size#fixedBlockLength", nullptr },
	{ RV_LV2 "ext/buf-size#powerOf2BlockLength", nullptr },
	{ RV_LV2 "lv2core#isLive", nullptr },
	{ RV_LV2 "lv2core#hardRTCapable", nullptr },
	{ RV_LV2 "lv2core#inPlaceBroken", nullptr },
};

#undef RV_LV2

template <size_t N>
const uri_type_t* by_uri(const uri_type_t (&table)[N], const char* uri) {
	for (const uri_type_t& k : table)
		if (strcmp(k.uri, uri) == 0) return &k;
	return nullptr;
}

} // namespace rv::lv2
