#pragma once
#include "reverb.hpp"
#include <pro.h>
#include <vector>

rv::Interface classify_from_exports(const std::vector<qstring>& exports);
bool check_vst2_ep(ea_t ea);

bool has_main(const std::vector<qstring>& exports);