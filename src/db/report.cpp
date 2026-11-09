#include "report.hpp"
#include <netnode.hpp>

namespace rv {

static constexpr const char* NODE = "$ reverb.report";
static constexpr uchar VERSION = 1;

void store_report(const std::vector<report::report_entry_t>& es) {
	bytevec_t b;
	b.pack_db(VERSION);
	b.pack_dd(uint32(es.size()));

	for (const report::report_entry_t& e : es) {
		b.pack_db(e.heading);
		b.pack_ea64(e.ea);
		b.pack_str(e.key);
		b.pack_str(e.value);
	}

	netnode n(NODE, 0, true);
	n.setblob(b.begin(), b.size(), 0, stag);
}

bool load_report(std::vector<report::report_entry_t>& out) {
	out.clear();

	netnode n(NODE);
	if (n == BADNODE) return false;

	bytevec_t b;
	if (n.getblob(&b, 0, stag) <= 0) return false;

	memory_deserializer_t d(b);
	if (d.unpack_db() != VERSION) return false;

	const uint32 count = d.unpack_dd();
	for (uint32 i = 0; i < count && !d.empty(); i++) {
		report::report_entry_t e;

		e.heading = d.unpack_db() != 0;
		e.ea = ea_t(d.unpack_ea64());
		d.unpack_str(&e.key);
		d.unpack_str(&e.value);

		out.push_back(std::move(e));
	}

	return !out.empty();
}

} // namespace rv
