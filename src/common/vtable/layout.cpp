#include "common/vtable/layout.hpp"
#include "util.hpp"
#include <bytes.hpp>
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

} // namespace rv::vtbl
