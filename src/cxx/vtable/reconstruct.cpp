#include "reconstruct.hpp"
#include "apply.hpp"
#include "core/log.hpp"
#include "model.hpp"
#include <utility>
#include <vector>

namespace rv::vtbl {

static std::vector<std::pair<ea_t, qstring>> g_pinned;

void pin(ea_t vt, const char* vtbl_type) {
	g_pinned.emplace_back(vt, vtbl_type);
}

void reconstruct() {
	model m;
	if (m.add_rtti_vtables() > 0)
		rv_log("%u RTTI vtables", unsigned(m.vtables.size()));

	for (const auto& [vt, type] : g_pinned) {
		// only label vtables with a matching generated "Something_vtbl"
		if (!m.add(vt, type.c_str()))
			rv_log("Labelling vtable failed at %a", vt);
	}
	g_pinned.clear();

	if (m.vtables.empty()) return;

	m.collect();
	const label_stats st = apply(m);
	rv_log("Vtable labels: %s", format_stats(st).c_str());
}

} // namespace rv::vtbl
