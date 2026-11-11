#include "taint.hpp"
#include "core/log.hpp"
#include <algorithm>

namespace rv::taint {

static std::vector<seed_t> g_seeds;

void add_seed(const seed_t& s) {
	if (s.func == BADADDR || s.spec == nullptr) return;

	const bool dup = std::any_of(g_seeds.begin(), g_seeds.end(),
		[&](const seed_t& o) {
			return o.func == s.func && o.spec->arg == s.spec->arg;
		});
	if (dup) return;

	rv_log("Taint seed: %s -> %a arg %d", s.spec->reason, s.func,
		s.spec->arg);
	g_seeds.push_back(s);
}

const std::vector<seed_t>& seeds() { return g_seeds; }

void clear_seeds() { g_seeds.clear(); }

} // namespace rv::taint
