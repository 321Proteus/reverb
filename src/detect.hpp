#pragma once
#include "core/reverb.hpp"
#include <pro.h>
#include <vector>

/**
 * @brief Classify the program export functions/data symbols found by IDA,
 * trying to match their names with what's expected from a plugin format
 *
 * rv::find_fw_signals()
 *
 * @param exports names of exported entry points to classify
 * @return rv::Interface set made of the formats found, empty if none is
 * applicable
 */
rv::InterfaceSet classify_from_exports(const std::vector<qstring>& exports);