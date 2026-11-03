#include "refs.hpp"
#include "segments.hpp"
#include "read.hpp"
#include <cstring>
#include <nalt.hpp>

namespace rv {

void ref_index::build() {
	ptrs_.clear();
	rvas_.clear();

	const int psz = ptr_size();
	const bool want_rva = psz == 8 && inf_get_filetype() == f_PE;
	const ea_t ib = get_imagebase();

	for (segment_t* s = get_first_seg(); s != nullptr;
		s = get_next_seg(s->start_ea)) {
		if (!is_data_seg(s)) continue;

		const asize_t len = s->size();
		bytevec_t buf;
		buf.resize(len);
		if (get_bytes(buf.begin(), ssize_t(len), s->start_ea, GMB_READALL) <= 0)
			continue;

		// RVAs sit at 4-aligned addresses, pointers at pointer-aligned ones
		for (asize_t i = (4 - s->start_ea % 4) % 4; i + 4 <= len; i += 4) {
			const ea_t at = s->start_ea + i;

			if (want_rva) {
				uint32 r;
				memcpy(&r, &buf[i], 4);
				if (r != 0 && in_data_seg(ib + r)) rvas_[ib + r].push_back(at);
			}

			if (at % psz != 0 || i + psz > len) continue;

			ea_t v;
			if (psz == 8) {
				uint64 q;
				memcpy(&q, &buf[i], 8);
				v = ea_t(q);
			} else {
				uint32 d;
				memcpy(&d, &buf[i], 4);
				v = ea_t(d);
			}
			if (v != 0 && in_data_seg(v)) ptrs_[v].push_back(at);
		}
	}
}

static const std::vector<ea_t>* lookup(const ref_index::map_t& m, ea_t t) {
	auto it = m.find(t);
	return it == m.end() ? nullptr : &it->second;
}

const std::vector<ea_t>* ref_index::ptr_refs(ea_t t) const {
	return lookup(ptrs_, t);
}

const std::vector<ea_t>* ref_index::rva_refs(ea_t t) const {
	return lookup(rvas_, t);
}

} // namespace rv
