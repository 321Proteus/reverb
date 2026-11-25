#pragma once

#include <pro.h>
#include <vector>

/**
 * @file report.hpp
 * @brief The report of what was found: a flat list of entries the panel shows.
 */
namespace rv::report {

/** @brief Kind of a report entry */
enum entry_kind_t : uchar {
	HEADING, /**< Title of a section */
	ROW,	 /**< A key and its value */
	GROUP,	 /**< A row that folds the CHILD rows right after it */
	CHILD,	 /**< A row under a GROUP */
};

/** @brief One line of the report */
struct report_entry_t {
	entry_kind_t kind = ROW;
	qstring key; /**< The title, for a heading */
	qstring value;
	ea_t ea = BADADDR; /**< Double-click destination */
};

/** @brief Removes all entries */
void clear();

/** @return All entries, in order */
const std::vector<report_entry_t>& entries();

/** @brief Starts a section titled @p title; double-click jumps to @p ea */
void heading(const qstring& title, ea_t ea = BADADDR);

/** @brief Adds a row; double-click jumps to @p ea */
void row(const qstring& key, const qstring& value, ea_t ea = BADADDR);

/** @brief Replaces all entries, e.g. with the ones stored in the IDB */
void assign(std::vector<report_entry_t> es);

/**
 * @brief Adds a collapsed row whose value sums up the child() rows added after
 * it
 */
void group(const qstring& key, const qstring& value, ea_t ea = BADADDR);

/** @brief Adds a row under the last group() */
void child(const qstring& key, const qstring& value, ea_t ea = BADADDR);

/** @brief Adds a row with the value `name (ea)`, jumping to @p ea; `(none)` for
 * BADADDR */
void addr_row(const qstring& key, ea_t ea);

} // namespace rv::report
