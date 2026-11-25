/**
 * @file report.cpp
 * @brief Report storage.
 */
#include "report.hpp"
#include <name.hpp>

namespace rv::report {

/** @brief The entries of the current report */
static std::vector<report_entry_t> g_entries;

void clear() { g_entries.clear(); }

const std::vector<report_entry_t>& entries() { return g_entries; }

void heading(const qstring& title, ea_t ea) {
	g_entries.push_back({ HEADING, title, qstring(), ea });
}

void row(const qstring& key, const qstring& value, ea_t ea) {
	g_entries.push_back({ ROW, key, value, ea });
}

void group(const qstring& key, const qstring& value, ea_t ea) {
	g_entries.push_back({ GROUP, key, value, ea });
}

void child(const qstring& key, const qstring& value, ea_t ea) {
	g_entries.push_back({ CHILD, key, value, ea });
}

void addr_row(const qstring& key, ea_t ea) {
	if (ea == BADADDR) {
		row(key, "(none)");
		return;
	}

	qstring n, v;
	get_ea_name(&n, ea);
	v.sprnt("%s (%a)", n.empty() ? "?" : n.c_str(), ea);
	row(key, v, ea);
}

void assign(std::vector<report_entry_t> es) { g_entries = std::move(es); }

} // namespace rv::report
