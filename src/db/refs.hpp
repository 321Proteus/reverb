#pragma once

#include <pro.h>
#include <unordered_map>
#include <vector>

namespace rv {

/*
	Reverse map of the pointer values (and 32-bit RVAs for 64-bit PE) stored
	in data segments, keyed by target. Only targets inside data segments are
	kept. Built once, then used for every query (instead of IDA xrefs)
*/
class ref_index {
public:
	using map_t = std::unordered_map<ea_t, std::vector<ea_t>>;

	void build();
	const std::vector<ea_t>* ptr_refs(ea_t target) const;
	const std::vector<ea_t>* rva_refs(ea_t target) const;
	const map_t& ptrs() const { return ptrs_; }

private:
	map_t ptrs_, rvas_;
};

} // namespace rv
