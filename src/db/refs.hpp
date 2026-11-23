#pragma once
/**
 * @file refs.hpp
 * @brief Reverse pointer index of the data segments.
 */

#include <pro.h>
#include <unordered_map>
#include <vector>

namespace rv {

/**
 * @brief Reverse map of the pointer values (and 32-bit RVAs for 64-bit PE)
 * stored in data segments, keyed by target
 *
 * Only targets inside data segments are kept. Built once, then used for every
 * query (instead of IDA xrefs).
 */
class ref_index {
public:
	using map_t = std::unordered_map<ea_t, std::vector<ea_t>>;

	/** @brief Scans all data segments; replaces any earlier contents */
	void build();
	/** @return Addresses of pointers to @p target, or null if none */
	const std::vector<ea_t>* ptr_refs(ea_t target) const;
	/** @return Addresses of 32-bit RVAs of @p target (64-bit PE only), or null */
	const std::vector<ea_t>* rva_refs(ea_t target) const;
	/** @return The whole pointer map */
	const map_t& ptrs() const { return ptrs_; }

private:
	map_t ptrs_, rvas_;
};

} // namespace rv
