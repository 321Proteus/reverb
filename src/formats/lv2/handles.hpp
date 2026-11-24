#pragma once

#include <pro.h>
#include <vector>

namespace rv::lv2 {

// A function table whose callbacks get the instance of one descriptor
struct handle_use_t {
	ea_t table = BADADDR;
	qstring table_type;	   // LV2_Descriptor, LV2_State_Interface...
	qstring instance_type; // lv2_plugin_instance...
};

/*
	Types each LV2_Handle / LV2UI_Handle parameter (by the SDK prototype of
	the member) of every callback in the tables as a pointer to its
	instance type, and the same parameter of the function a callback only
	forwards it to. A function that tables of different instances share, or
	that is referenced outside its table, is left alone
*/
void type_handles(const std::vector<handle_use_t>& uses);

} // namespace rv::lv2
