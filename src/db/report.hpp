#pragma once

#include "ui/report.hpp"
#include <vector>

namespace rv {

// Store the report in the IDB
void store_report(const std::vector<report::report_entry_t>& es);

// False if the database has no stored report
bool load_report(std::vector<report::report_entry_t>& out);

} // namespace rv
