#pragma once

#include "db/segments.hpp"
#include <pro.h>
#include <typeinf.hpp>

namespace rv::vtbl {

// is t non-null, loaded and executable?
inline bool is_slot_target(ea_t t) {
	return t != 0 && t != BADADDR && is_loaded(t) && in_exec_seg(t);
}

/*
	Slots holding slot target pointers. A slot after the first
	with an xref starts something else (the next vatble or its locator)
*/
size_t extent(ea_t vt, size_t max = 1024);

ea_t read_slot(ea_t vt, size_t i);

/*
	At least two slots in a row. Only a hint (also matches .init_array, CRT
	initializer and dispatch tables), never proof of a vtable on its own
*/
bool looks_like(ea_t ea);

/*
	Vtable slots holding func. Xrefs from inside a struct come from its first
	byte, so every data item that references func is scanned for slots equal
	to it; this also drops 4-byte RVA references (.pdata, CFG, EH metadata)
*/
int slot_refs(ea_t func);

// Slot target is __cxa_pure_virtual / _purecall, through one thunk hop
bool is_pure_virtual(ea_t target);

/*
	Consecutive slots from address point vt, imports included. Unlike
	extent, needs no xrefs, so it works before auto-analysis finishes
*/
int vtable_run_length(ea_t vt, int max_entries = 1024);

// fn(const udm_t&, size_t byte_offset) for every slot of a _vtbl type, base
// classes flattened
template <class F>
void for_each_member(
	const tinfo_t& vtbl, F&& fn, size_t base = 0, int depth = 0) {
	udt_type_data_t udt;
	if (depth > 16 || !vtbl.get_udt_details(&udt)) return;

	for (const udm_t& m : udt) {
		if (m.is_gap()) continue;

		const size_t off = base + size_t(m.offset / 8);
		if (m.is_baseclass()) for_each_member(m.type, fn, off, depth + 1);
		else
			fn(m, off);
	}
}

} // namespace rv::vtbl
