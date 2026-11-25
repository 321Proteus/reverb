#pragma once

/**
 * @file apply.hpp
 * @brief Writes vtable types, slot function names and prototypes to the IDB.
 */

#include "model.hpp"
#include <initializer_list>

namespace rv::vtbl {

/**
 * @brief What apply() is allowed to change
 */
struct label_policy {
	bool rename = true, set_prototypes = true, comment_shared = true;
	bool use_shifted_this =
		false; /**< __shifted(Class, off) for secondary vtables */
};

/**
 * @brief Changes made by apply() (except the per-kind function counts)
 */
// clang-format off
struct label_stats {
	int vtables = 0,
		named = 0,
		prototyped = 0,
		inherited = 0,
		folded = 0,
		thunks = 0,
		purecall = 0,
		skipped = 0,
		name_taken = 0;
};
// clang-format on

/**
 * @brief Types each vtable as its interface `_vtbl`, then names and prototypes
 * the slot functions (dummy/non-user-defined only)
 *
 * @param m Collected model, @sa model::collect()
 * @param p What may be changed
 * @return Counts of the changes made
 */
label_stats apply(const model& m, const label_policy& p = {});

/**
 * @brief One-line summary of @p s for logging
 */
qstring format_stats(const label_stats& s);

/**
 * @brief The first candidate vtable type that fits the extent at @p ea
 *
 * @return Its name, or empty
 */
qstring pick_type(ea_t ea, std::initializer_list<const char*> candidates);

} // namespace rv::vtbl

namespace rv {

/**
 * @brief Local struct type of a class, by full name or by a unique short name
 *
 * @return The type, or an empty tinfo_t if missing or ambiguous
 */
tinfo_t find_class_type(const qstring& cls);

} // namespace rv
