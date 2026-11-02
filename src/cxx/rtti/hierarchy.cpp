#include "hierarchy.hpp"
#include "core/log.hpp"
#include <set>

namespace rv::rtti {

void class_hierarchy::build() {
	refs_.build();
	nodes_.clear();
	by_name_.clear();
	by_vtable_.clear();

	for (ea_t ti : collect_typeinfos(refs_)) {
		class_node n;
		if (!parse_typeinfo(refs_, ti, &n.info)) continue;
		n.vtables = vtables_of(refs_, ti);
		nodes_.emplace(ti, std::move(n));
	}

	// Second pass: name/vtable lookups and the inverted base -> derived edges.
	// Only direct bases are linked
	size_t with_vtables = 0;
	for (auto& [ti, n] : nodes_) {
		by_name_.emplace(n.info.name, ti);
		if (!n.vtables.empty()) with_vtables++;
		for (ea_t vt : n.vtables)
			by_vtable_.emplace(vt, ti);

		for (const base_info& b : n.info.bases) {
			if (!b.direct) continue;
			auto it = nodes_.find(b.typeinfo);
			if (it != nodes_.end()) it->second.derived.push_back(ti);
		}
	}

	rv_log("RTTI: %u classes, %u with vtables", unsigned(nodes_.size()),
		unsigned(with_vtables));
}

const class_node* class_hierarchy::find(ea_t ti) const {
	auto it = nodes_.find(ti);
	return it == nodes_.end() ? nullptr : &it->second;
}

const class_node* class_hierarchy::find_class(const qstring& name) const {
	auto it = by_name_.find(name);
	return it == by_name_.end() ? nullptr : find(it->second);
}

std::vector<const class_node*> class_hierarchy::derived_of(
	ea_t ti, bool recursive) const {
	std::vector<const class_node*> out;
	std::set<ea_t> seen{ti};
	std::vector<ea_t> work{ti};

	while (!work.empty()) {
		const class_node* n = find(work.back());
		work.pop_back();
		if (n == nullptr) continue;

		for (ea_t d : n->derived) {
			if (!seen.insert(d).second) continue; // diamonds
			if (const class_node* dn = find(d)) out.push_back(dn);
			if (recursive) work.push_back(d);
		}
	}
	return out;
}

std::vector<const base_info*> class_hierarchy::direct_bases(ea_t ti) const {
	std::vector<const base_info*> out;
	if (const class_node* n = find(ti))
		for (const base_info& b : n->info.bases)
			if (b.direct) out.push_back(&b);
	return out;
}

ea_t class_hierarchy::class_of_vtable(ea_t vt) const {
	auto it = by_vtable_.find(vt);
	return it == by_vtable_.end() ? BADADDR : it->second;
}

} // namespace rv::rtti
