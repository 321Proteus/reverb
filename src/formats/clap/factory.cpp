#include "factory.hpp"
#include <bytes.hpp>
#include <funcs.hpp>
#include "common/function_iterator.hpp"
#include <pro.h>
#include "common/analysis.hpp"
#include "common/stores.hpp"
#include "exts.hpp"

namespace rv::clap {

// Return whether every func pointer member points into code and the struct is
// loaded
bool is_fn_table(ea_t ea, const tinfo_t& tif) {
	const size_t sz = tif.get_size();
	if (sz == BADSIZE || !is_loaded(ea) || !is_loaded(ea + sz - 1))
		return false;

	udt_type_data_t udt;
	if (!tif.get_udt_details(&udt)) return false;

	int fns = 0;
	for (const udm_t& m : udt) {
		if (!m.type.is_funcptr()) continue;
		if (!is_code_addr(read_ptr(ea + m.offset / 8))) return false;
		fns++;
	}
	return fns > 0;
}

// Compare by length, not up to a NUL (Rust and some C++ builds)
static const id_type_t* factory_id_at(ea_t ea) {
	for (const id_type_t& k : CLAP_FACTORIES) {
		const size_t n = strlen(k.id);
		qstring buf;
		buf.resize(n);
		if (get_bytes(buf.begin(), n, ea) == ssize_t(n) &&
			memcmp(buf.c_str(), k.id, n) == 0)
			return &k;
	}
	return nullptr;
}

std::vector<factory_hit_t> find_factories(ea_t gf_ea) {
	func_t* pfn = rv::ensure_func(gf_ea);
	if (pfn == nullptr) return {};

	tinfo_t probe; // shared by stable factory types
	if (!probe.get_named_type(nullptr, "clap_plugin_factory_t")) return {};

	std::vector<const id_type_t*> ids;
	std::vector<ea_t> tables;
	std::set<ea_t> seen;

	rv_fii_t fii;
	for (bool ok = rv_fii_set(fii, pfn->start_ea); ok; ok = fii.next_code()) {
		xrefblk_t xb;
		for (bool x = xb.first_from(fii.current(), XREF_DATA); x;
			x = xb.next_from()) {
			ea_t t = xb.to;
			if (!seen.insert(t).second) continue;

			if (const id_type_t* k = factory_id_at(t)) {
				ids.push_back(k);
				continue;
			}
			// one hop through a GOT slot (exported factory in PIC)
			if (ea_t p; !is_fn_table(t, probe) && load_ptr_const(t, p)) t = p;
			if (is_fn_table(t, probe)) tables.push_back(t);
		}
	}

	std::vector<factory_hit_t> hits;
	if (!ids.empty() && ids.size() == tables.size()) {
		// if (!strcmp(id, A)) return &a; if (!strcmp(id, B)) return &b;
		for (size_t i = 0; i < ids.size(); i++)
			hits.push_back({ tables[i], ids[i] });
	} else if (ids.empty() && tables.size() == 1) {
		rv_log("gf_ea at %a has no id check, assuming plugin factory", gf_ea);
		hits.push_back({ tables[0], &CLAP_FACTORIES[0] });
	} else {
		rv_log("gf_ea at %a: %u id(s), %u factory struct(s), not paired", gf_ea,
			unsigned(ids.size()), unsigned(tables.size()));
	}
	return hits;
}

} // namespace rv::clap