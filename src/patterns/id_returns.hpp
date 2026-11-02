#pragma once

#include <pro.h>
#include <vector>

namespace rv {

// A string id compared with a function argument, and the object returned when
// it matches
struct id_return_t {
	qstring id;
	ea_t ea = BADADDR; // returned object
	bool conditional = false; // !cmp(id, x) && something_else()
};

/*
	Ids compared with argument arg of the function at fn_ea. The callee is
	not checked, so strcmp, memcmp and inlined helpers all count. Accepts
	cmp() == 0 and !cmp() (the then-branch), cmp() != 0 and a bare cmp() (the
	else-branch, or the next statement when there is no else and the
	then-branch returns null). In A || B every id matches the then-branch,
	in A && B the id is conditional. The match must end in a return
*/
std::vector<id_return_t> find_id_returns(ea_t fn_ea, int arg = 0);

} // namespace rv
