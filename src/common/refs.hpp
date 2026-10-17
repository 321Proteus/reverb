#pragma once

#include <pro.h>
#include <bytes.hpp>
#include <segment.hpp>
#include <unordered_map>
#include <vector>

namespace rv {

/*
	Initialized, non-code segments: constants, vtables, RTTI, string literals.
	Mach-O __TEXT data sections inherit R|X perms, so perms only decide
	when the segment type says nothing
*/
inline bool is_data_seg(const segment_t* s) {
	if (s == nullptr) return false;
	if (s->type == SEG_CODE || s->type == SEG_XTRN || s->type == SEG_BSS)
		return false;
	if (s->type == SEG_NORM && s->perm != 0)
		return (s->perm & SEGPERM_EXEC) == 0;
	return true;
}

inline bool in_data_seg(ea_t ea) {
	return is_loaded(ea) && is_data_seg(getseg(ea));
}

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
