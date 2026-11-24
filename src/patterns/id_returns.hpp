#pragma once

#include <hexrays.hpp>
#include <pro.h>
#include <vector>

namespace rv {

// A string id compared with a function argument, and the object returned when
// it matches
struct id_return_t {
	qstring id;
	ea_t ea = BADADDR;		  // returned object
	bool conditional = false; // !cmp(id, x) && something_else()
};

/*
	Ids compared with argument arg of the function at fn_ea. The callee is
	not checked, so strcmp, memcmp and inlined helpers all count. Accepts
	cmp() == 0 and !cmp() (the then-branch), cmp() != 0 and a bare cmp() (the
	else-branch, or the next statement when there is no else and the
	then-branch returns null). Inlined compare loops count too, through
	their result variable. In A || B every id matches the then-branch, in
	A && B the id is conditional. The match must end in a return, possibly
	behind a goto or a nested if (other) return x; (also conditional)
*/
std::vector<id_return_t> find_id_returns(ea_t fn_ea, int arg = 0);

/*
	An if condition that compares with a string literal, in the forms above:
	cmp(x, "lit") == 0 and !cmp() match in the then-branch, cmp() != 0 and a
	bare cmp() in the else-branch. -> the call (its other arguments are what
	is compared), nullptr when c is no such compare
*/
cexpr_t* literal_cmp(cexpr_t* c, bool& in_then, qstring& lit);

} // namespace rv
