/**
 * @file layout.cpp
 * @brief Raw vtable layout helpers, see layout.hpp.
 */
#include "layout.hpp"
#include "db/segments.hpp"
#include "db/read.hpp"
#include <bytes.hpp>
#include <funcs.hpp>
#include <name.hpp>
#include <segment.hpp>
#include <xref.hpp>

namespace rv::vtbl {

size_t extent(ea_t vt, size_t max) {
	size_t n = 0;

	for (; n < max; n++) {
		const ea_t slot = vt + n * ptr_size();
		if (!is_loaded(slot)) break;
		if (n > 0 && has_xref(get_flags(slot))) break;
		if (!is_slot_target(read_ptr(slot))) break;
	}
	return n;
}

ea_t read_slot(ea_t vt, size_t i) { return read_ptr(vt + i * ptr_size()); }

bool looks_like(ea_t ea) {
	for (size_t i = 0; i < 2; i++) {
		const ea_t slot = ea + i * ptr_size();
		if (!is_loaded(slot) || !is_slot_target(read_ptr(slot))) return false;
	}
	return true;
}

int slot_refs(ea_t func) {
	const int psz = ptr_size();
	int refs = 0;

	xrefblk_t xb;
	for (bool ok = xb.first_to(func, XREF_DATA); ok; ok = xb.next_to()) {
		if (xb.type != dr_O || is_code(get_flags(xb.from))) continue;

		const ea_t end = get_item_end(xb.from);
		for (ea_t ref = xb.from; ref + psz <= end; ref += psz)
			if (read_ptr(ref) == func) refs++;
	}
	return refs;
}

/*
	Slot target is __cxa_pure_virtual (Itanium) or _purecall (MSVC). Follows
	one thunk hop, since imports are usually reached through a stub, and the
	IAT/GOT slot of the import keeps its name even in stripped binaries
*/
bool is_pure_virtual(ea_t target) {
	for (int hop = 0; hop < 2 && target != BADADDR; hop++) {
		const qstring n = get_name(target);
		if (n.find("__cxa_pure_virtual") != qstring::npos ||
			n.find("purecall") != qstring::npos)
			return true;

		func_t* f = get_func(target);
		if (f == nullptr || (f->flags & FUNC_THUNK) == 0) break;

		ea_t fptr = BADADDR;
		const ea_t t = calc_thunk_func_target(f, &fptr);
		target = (t != BADADDR)
					 ? t
					 : fptr; // indirect thunk: name of the import slot
	}
	return false;
}

// Number of consecutive slots from address point vt
int vtable_run_length(ea_t vt, int max_entries) {
	const int psz = ptr_size();
	int n = 0;

	for (; n < max_entries; n++) {
		const ea_t slot = vt + n * psz;
		if (!is_loaded(slot)) break;

		const ea_t t = read_ptr(slot);
		if (t == 0) break;

		const segment_t* s = getseg(t);
		const bool ext = s != nullptr && s->type == SEG_XTRN;
		if (!ext && !in_exec_seg(t)) break;
	}
	return n;
}

} // namespace rv::vtbl
