#pragma once

#include <pro.h>
#include <vector>

namespace rv::report {

enum entry_kind_t : uchar {
	HEADING,
	ROW,
	GROUP, // a row that folds the CHILD rows right after it
	CHILD,
};

struct report_entry_t {
	entry_kind_t kind = ROW;
	qstring key; // the title, for a heading
	qstring value;
	ea_t ea = BADADDR; // doubleclick destination
};

void clear();
const std::vector<report_entry_t>& entries();

void heading(const qstring& title, ea_t ea = BADADDR);
void row(const qstring& key, const qstring& value, ea_t ea = BADADDR);

// Replaces all entries, e.g. with the ones stored in the IDB
void assign(std::vector<report_entry_t> es);

/*
	A collapsed row whose value sums up the child rows added after it
*/
void group(const qstring& key, const qstring& value, ea_t ea = BADADDR);
void child(const qstring& key, const qstring& value, ea_t ea = BADADDR);

// "name (ea)", jumping to ea
void addr_row(const qstring& key, ea_t ea);

} // namespace rv::report
