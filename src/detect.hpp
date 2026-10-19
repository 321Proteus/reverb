#pragma once
#include "reverb.hpp"
#include <pro.h>
#include <vector>

rv::InterfaceSet classify_from_exports(const std::vector<qstring>& exports);
bool check_vst2_ep(ea_t ea);