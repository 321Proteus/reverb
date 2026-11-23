#pragma once
/**
 * @file report.hpp
 * @brief Persistence of the analysis report inside the IDB.
 */

#include "ui/report.hpp"
#include <vector>

namespace rv {

/**
 * @brief Stores the report in the IDB
 */
void store_report(const std::vector<report::report_entry_t>& es);

/**
 * @brief Loads the stored report into @p out
 *
 * @return false if the database has no (valid) stored report
 */
bool load_report(std::vector<report::report_entry_t>& out);

} // namespace rv
