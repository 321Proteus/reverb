#pragma once

#include <pro.h>
#include <vector>

namespace rv::report {

struct report_entry_t {
	bool heading = false;
	qstring key; // the title, for a heading
	qstring value;
	ea_t ea = BADADDR; // doubleclick destination
};

void clear();
const std::vector<report_entry_t>& entries();

void heading(const qstring& title, ea_t ea = BADADDR);
void row(const qstring& key, const qstring& value, ea_t ea = BADADDR);

// "name (ea)", jumping to ea
void addr_row(const qstring& key, ea_t ea);

} // namespace rv::report
