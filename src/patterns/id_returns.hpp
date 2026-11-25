#pragma once

#include <pro.h>
#include <vector>

/**
 * @file id_returns.hpp
 * @brief Finding the objects a function returns for each string id it is asked.
 */

namespace rv {

/**
 * @brief A string id compared with a function argument, and the object returned
 * when it matches
 */
struct id_return_t {
	qstring id;
	ea_t ea = BADADDR;		  /**< Returned object */
	bool conditional = false; /**< `!cmp(id, x) && something_else()` */
};

/**
 * @brief ID compared with an argument of a function, and what is returned for
 * each
 *
 * The callee is not checked, so `strcmp`, `memcmp` and inlined helpers all
 * count. Accepts `cmp() == 0` and `!cmp()` (the then-branch), `cmp() != 0` and
 * a bare `cmp()` (the else-branch, or the next statement when there is no else
 * and the then-branch returns null). Inlined compare loops count too, through
 * their result variable. In `A || B` every id matches the then-branch, in
 * `A && B` the id is conditional. The match must end in a return, possibly
 * behind a goto or a nested `if (other) return x;` (also conditional).
 *
 * @param fn_ea Function to analyze
 * @param arg   Position of the compared argument
 */
std::vector<id_return_t> find_id_returns(ea_t fn_ea, int arg = 0);

} // namespace rv
