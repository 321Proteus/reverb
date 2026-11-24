#pragma once

#include <pro.h>

namespace rv::lv2 {

// The host feature URIs instantiate compares LV2_Feature::URI with
qstrvec_t find_features(ea_t inst);

/*
	Types and names what instantiate stores the data of a known feature in
	(urid_map = features[i]->data under strcmp(features[i]->URI, map URI)):
	a local or a global. Needs instantiate's prototype applied, so this runs
	at label time. Adds a report row per feature under the current heading
*/
void label_features(ea_t inst, const qstrvec_t& features);

} // namespace rv::lv2
