#include "taint.hpp"
#include "core/log.hpp"
#include "decomp/analysis.hpp"
#include "formats/format.hpp"
#include <algorithm>
#include <cstring>
#include <funcs.hpp>
#include <typeinf.hpp>

namespace rv::taint {

static bool is_primitive(const char* name) {
	return !strcmp(name, "float") || !strcmp(name, "double");
}

static bool type_is(const tinfo_t& t, const char* name) {
	if (!strcmp(name, "float")) return t.is_float();
	if (!strcmp(name, "double")) return t.is_double();
	return short_type_name(t) == name;
}

// t is owner or has it among its (transitive) base classes
static bool derives(const tinfo_t& t, const char* owner, int depth = 0) {
	if (type_is(t, owner)) return true;
	if (depth > 8) return false;

	udt_type_data_t u;
	if (!t.is_udt() || !t.get_udt_details(&u)) return false;
	for (const udm_t& m : u)
		if (m.is_baseclass() && derives(m.type, owner, depth + 1)) return true;
	return false;
}

/*
	The tainted arg must always match. The owner (arg 0) is checked only for
	float/double args, which alone are too common. vtbl::apply may have
	replaced it with a concrete class that does not declare its bases
*/
static bool matches(const func_type_data_t& ft, const seed_spec_t& s) {
	if (s.arg < 0 || ft.size() <= size_t(s.arg)) return false;

	const tinfo_t& a = ft[s.arg].type;
	if (!a.is_ptr() || !type_is(innermost(a), s.arg_type)) return false;
	if (!is_primitive(s.arg_type)) return true;

	const tinfo_t& o = ft[0].type;
	return o.is_ptr() && derives(o.get_pointed_object(), s.owner);
}

std::vector<seed_t> find_seeds(InterfaceSet is) {
	std::vector<const seed_spec_t*> specs;
	is.for_each([&](Interface iface) {
		const FormatModule* m = get_format_module(iface);
		if (m == nullptr) return;
		for (size_t i = 0; i < m->n_seeds; i++)
			specs.push_back(&m->seeds[i]);
	});

	std::vector<seed_t> out;
	std::vector<std::pair<ea_t, int>> seen; // (thunk target, arg)
	if (specs.empty()) return out;

	const size_t n = get_func_qty();
	for (size_t i = 0; i < n; i++) {
		const func_t* f = getn_func(i);
		if (f == nullptr || (f->flags & FUNC_LIB) != 0) continue;

		tinfo_t t;
		func_type_data_t ft;
		if (!get_tinfo(&t, f->start_ea) || !t.get_func_details(&ft)) continue;

		for (const seed_spec_t* s : specs) {
			if (!matches(ft, *s)) continue;

			const std::pair<ea_t, int> key{ resolve_thunk(f->start_ea),
				s->arg };
			if (std::find(seen.begin(), seen.end(), key) != seen.end())
				continue;
			seen.push_back(key);

			rv_log(
				"Taint seed: %s -> %a arg %d", s->reason, f->start_ea, s->arg);
			out.push_back({ f->start_ea, s });
		}
	}
	return out;
}

} // namespace rv::taint
