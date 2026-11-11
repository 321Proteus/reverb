#include "taint.hpp"
#include "core/log.hpp"

namespace rv::taint {

static constexpr int MAX_DEPTH = 8;

qstring short_type_name(const tinfo_t& t) {
	qstring n;
	if (!t.get_final_type_name(&n)) t.get_type_name(&n);

	for (size_t i = n.length(); i > 0; i--)
		if (n[i - 1] == ':') return n.substr(i);
	return n;
}

tinfo_t innermost(tinfo_t t) {
	for (int i = 0; i < MAX_DEPTH && (t.is_ptr() || t.is_array()); i++)
		t = t.get_ptrarr_object();
	return t;
}

void run(InterfaceSet is) {
	const std::vector<seed_t> seeds = find_seeds(is);
	rv_log("Taint: %u seed(s) found", unsigned(seeds.size()));
}

} // namespace rv::taint
