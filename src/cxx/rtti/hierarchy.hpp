#pragma once

#include "abi.hpp"
#include <map>
#include <vector>

namespace rv::rtti {

struct class_node {
	class_info info; // bases carry typeinfo addresses and direct flags
	std::vector<ea_t> vtables; // address points, primary and secondary
	std::vector<ea_t>
		derived; // typeinfos of classes directly derived from this one
};

class class_hierarchy {
public:
	// One pass over the IDB. Run once afteer auto-analysis is done, so vtable
	// slots point at analyzed code.
	void build();

	const class_node* find(ea_t ti) const;

	// Demangled name, first match if several
	const class_node* find_class(const qstring& name) const;

	// Classes deriving from ti, recursive also returns indirect ones
	std::vector<const class_node*> derived_of(
		ea_t ti, bool recursive = false) const;

	// Immediate bases of ti
	std::vector<const base_info*> direct_bases(ea_t ti) const;

	// Typeinfo of the class owning the vtable at vt (primary or secondary)
	ea_t class_of_vtable(ea_t vt) const;

	const ref_index& refs() const { return refs_; }
	size_t size() const { return nodes_.size(); }

private:
	ref_index refs_;
	std::map<ea_t, class_node> nodes_;
	std::multimap<qstring, ea_t> by_name_;
	std::map<ea_t, ea_t> by_vtable_;
};

} // namespace rv::rtti
