#include "factory.hpp"
#include <bytes.hpp>
#include <funcs.hpp>
#include "common/function_iterator.hpp"
#include <pro.h>
#include "common/analysis.hpp"
#include "common/stores.hpp"
#include "common/id_dispatch.hpp"
#include <algorithm>
#include "exts.hpp"

namespace rv::clap {

// Return whether every func pointer member points into local code and the
// struct is loaded. Imports are rejected to avoid GOT slots like strcmp getting
// mistaken for tables
bool is_fn_table(ea_t ea, const tinfo_t& tif) {
	const size_t sz = tif.get_size();
	if (sz == BADSIZE || !is_loaded(ea) || !is_loaded(ea + sz - 1))
		return false;

	udt_type_data_t udt;
	if (!tif.get_udt_details(&udt)) return false;

	int fns = 0;
	for (const udm_t& m : udt) {
		if (!m.type.is_funcptr()) continue;
		const ea_t fn = read_ptr(ea + m.offset / 8);
		if (!is_code_addr(fn) || in_extern_seg(fn)) return false;
		fns++;
	}
	return fns > 0;
}

static bool id_char(int c) {
	return qisalnum(c) || c == '.' || c == '-' || c == '_' || c == '/';
}

static const id_type_t* factory_id_at(ea_t ea) {
	const id_type_t* best = nullptr;

	for (const id_type_t& k : CLAP_FACTORIES) {
		const size_t n = strlen(k.id);
		qstring buf;
		buf.resize(n);

		if (get_bytes(buf.begin(), n, ea) != ssize_t(n) ||
			memcmp(buf.c_str(), k.id, n) != 0)
			continue;

		if (id_char(get_byte(ea + n))) continue; // longer id

		if (best == nullptr || n > strlen(best->id)) best = &k;
	}
	return best;
}

static const id_type_t* factory_by_id(const char* id) {
	for (const id_type_t& k : CLAP_FACTORIES)
		if (strcmp(k.id, id) == 0) return &k;
	return nullptr;
}

static const id_type_t* factory_by_name(const char* name) {
	for (const id_type_t& k : CLAP_FACTORIES)
		if (strcmp(k.name, name) == 0) return &k;
	return nullptr;
}

// A _COMPAT id and its stable id name the same struct, prefer the stable one
static const id_type_t* prefer(const id_type_t* a, const id_type_t* b) {
	const bool a_compat = qstring(a->name).ends_with("_COMPAT");
	const bool b_compat = qstring(b->name).ends_with("_COMPAT");
	return a_compat && !b_compat ? b : a;
}

// Fallback when the ctree has no id checks: pair the ids and factory structs
// that get_factory references
static std::vector<factory_hit_t> xref_factories(ea_t gf_ea) {
	func_t* pfn = rv::ensure_func(gf_ea);
	if (pfn == nullptr) return {};

	tinfo_t probe; // shared by stable factory types
	if (!probe.get_named_type(nullptr, "clap_plugin_factory_t")) return {};

	std::vector<const id_type_t*> ids; // one per factory type
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
				auto it = std::find_if(
					ids.begin(), ids.end(), [&](const id_type_t* o) {
						return strcmp(o->type, k->type) == 0;
					});
				if (it == ids.end()) ids.push_back(k);
				else
					*it = prefer(*it, k);
				continue;
			}
			// one hop through a GOT slot (exported factory in PIC)
			if (ea_t p; !is_fn_table(t, probe) && load_ptr_const(t, p)) t = p;
			if (is_fn_table(t, probe)) tables.push_back(t);
		}
	}

	if (ids.size() == 1 && tables.size() == 1) { // cmov-compiled strcmp
		rv_log("get_factory at %a: paired %s by xrefs", gf_ea, ids[0]->name);
		return { { tables[0], ids[0] } };
	}

	if (ids.empty() && tables.size() == 1) {
		rv_log("get_factory at %a has no id check, assuming plugin factory",
			gf_ea);
		return { { tables[0], factory_by_name("PLUGIN"), true } };
	}

	rv_log("get_factory at %a: %u id(s), %u factory struct(s), unpaired", gf_ea,
		unsigned(ids.size()), unsigned(tables.size()));
	return {};
}

std::vector<factory_hit_t> find_factories(ea_t gf_ea) {
	const std::vector<rv::id_return_t> rets = rv::find_id_returns(gf_ea);
	if (rets.empty()) return xref_factories(gf_ea);

	std::vector<factory_hit_t> hits;
	for (const rv::id_return_t& r : rets) {

		const id_type_t* kind = factory_by_id(r.id.c_str());
		if (kind == nullptr) {
			rv_log("Unknown CLAP factory \"%s\" at %a", r.id.c_str(), r.ea);

			qstring cmt;
			cmt.sprnt("CLAP factory \"%s\" (type unknown)", r.id.c_str());
			set_cmt(r.ea, cmt.c_str(), true);
			continue;
		}

		tinfo_t tif;
		if (!tif.get_named_type(nullptr, kind->type)) continue;

		ea_t ea = r.ea;
		if (ea_t p; !is_fn_table(ea, tif) && load_ptr_const(ea, p)) ea = p;

		if (!is_fn_table(ea, tif)) {
			rv_log("CLAP factory \"%s\" at %a is not a %s", r.id.c_str(), r.ea,
				kind->type);
			continue;
		}

		auto it = std::find_if(hits.begin(), hits.end(),
			[&](const factory_hit_t& h) { return h.ea == ea; });

		if (it == hits.end()) hits.push_back({ ea, kind });
		else
			it->kind = prefer(it->kind, kind);
	}
	return hits;
}

} // namespace rv::clap