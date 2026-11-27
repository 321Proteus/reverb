#pragma once
#include "core/reverb.hpp"
#include <string>
#include <vector>

/**
 * @file detect.hpp
 * @brief Plugin format detection from the exports
 */

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
rv::InterfaceSet classify_from_exports(const std::vector<std::string>& exports);

/**
 * @brief Generate a compile time string constant hash, for switching over
 * strings
 * Source: https://stackoverflow.com/questions/16388510
 */
constexpr unsigned int str_cth(const char* str, int h = 0) {
	return !str[h] ? 5381 : (str_cth(str, h + 1) * 33) ^ str[h];
}
