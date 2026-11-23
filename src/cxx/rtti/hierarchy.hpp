#pragma once

/**
 * @file hierarchy.hpp
 * @brief Database-wide class hierarchy built from RTTI
 */

#include "abi.hpp"
#include <map>
#include <vector>

namespace rv::rtti {

/**
 * @brief A class in the type hierarchy
 */
struct class_node {
	class_info info; /**< Bases carry typeinfo addresses and direct flags */
	std::vector<ea_t> vtables; /**< Address points, primary and secondary */
	std::vector<ea_t>
		derived; /**< Typeinfos of classes directly derived from this one */
};

/**
 * @brief Index of all RTTI classes with lookups by typeinfo, name and vtable
 */
class class_hierarchy {
public:
	/**
	 * @brief Scan the IDB once. Run after auto-analysis finishes so vtable slots
	 * point at analyzed code
	 */
	void build();

	/**
	 * @brief Node of typeinfo @p ti, or null.
	 */
	const class_node* find(ea_t ti) const;

	/**
	 * @brief Node by demangled name (first match if several), or null.
	 */
	const class_node* find_class(const qstring& name) const;

	/**
	 * @brief Classes deriving from @p ti; with @p recursive also indirect ones.
	 */
	std::vector<const class_node*> derived_of(
		ea_t ti, bool recursive = false) const;

	/**
	 * @brief Immediate bases of @p ti.
	 */
	std::vector<const base_info*> direct_bases(ea_t ti) const;

	/**
	 * @brief Typeinfo of the class owning the vtable at @p vt (primary or secondary), or BADADDR
	 */
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
