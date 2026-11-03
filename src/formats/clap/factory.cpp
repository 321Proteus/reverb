#include "factory.hpp"
#include "core/log.hpp"
#include "db/segments.hpp"
#include "db/read.hpp"
#include <bytes.hpp>
#include <funcs.hpp>
#include "db/func_iter.hpp"
#include <pro.h>
#include "decomp/analysis.hpp"
#include "decomp/stores.hpp"
#include "patterns/id_returns.hpp"
#include <algorithm>
#include <string>
#include "exts.hpp"

namespace rv::clap {

// Return whether every func pointer member points into local code, other
// pointer members don't, and the struct is loaded outside GOT. Runs of GOT
// slots (strcmp, local functions in PIC) and vtables are not tables
bool is_fn_table(ea_t ea, const tinfo_t& tif) {
	const size_t sz = tif.get_size();
	if (sz == BADSIZE || !is_loaded(ea) || !is_loaded(ea + sz - 1) ||
		in_got_seg(ea))
		return false;

	udt_type_data_t udt;
	if (!tif.get_udt_details(&udt)) return false;

	int fns = 0;
	for (const udm_t& m : udt) {
		const ea_t fn = read_ptr(ea + m.offset / 8);
		if (!m.type.is_funcptr()) {
			if (m.type.is_ptr() && is_code_addr(fn)) return false; // desc
			continue;
		}
		if (!is_code_addr(fn) || in_extern_seg(fn)) return false;
		fns++;
	}
	return fns > 0;
}

bool resolve_table(ea_t& ea, const tinfo_t& tif) {
	if (is_fn_table(ea, tif)) return true;
	ea_t p;
	if (!load_ptr_const(ea, p) || !is_fn_table(p, tif)) return false;
	ea = p;
	return true;
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

static constexpr size_t MIN_FRAGMENT = 4;
static constexpr size_t MAX_FRAGMENT = 16;

// id with its NUL, which SIMD compares check too
static std::string id_bytes(const id_type_t& k) {
	return std::string(k.id, strlen(k.id) + 1);
}

// Up to 16 bytes of text at ea, through the first NUL, that fit a known id
static bool read_fragment(ea_t ea, std::string& out) {
	char buf[MAX_FRAGMENT];
	if (get_bytes(buf, sizeof(buf), ea) != ssize_t(sizeof(buf))) return false;

	const char* nul = static_cast<const char*>(memchr(buf, 0, sizeof(buf)));
	out.assign(buf, nul != nullptr ? nul - buf + 1 : sizeof(buf));
	if (out.size() < MIN_FRAGMENT ||
		!is_text(qstring(buf, out.size() - (nul != nullptr))))
		return false;

	for (const id_type_t& k : CLAP_FACTORIES)
		if (id_bytes(k).find(out) != std::string::npos) return true;
	return false;
}

/*
	The id that pieces compared with SIMD loads come from.
	A _COMPAT id and its stable id count as one
*/
static const id_type_t* fragment_id(const std::vector<std::string>& frags) {
	const id_type_t* found = nullptr;
	for (const id_type_t& k : CLAP_FACTORIES) {
		const std::string id = id_bytes(k);
		size_t covered = 0;
		bool fits = true;
		for (const std::string& f : frags) {
			fits = fits && id.find(f) != std::string::npos;
			covered += f.size();
		}
		if (!fits || covered < id.size()) continue;

		if (found != nullptr && strcmp(found->type, k.type) != 0)
			return nullptr; // ambiguous
		found = found == nullptr ? &k : prefer(found, &k);
	}
	return found;
}

static const id_type_t* factory_by_name(const char* name) {
	for (const id_type_t& k : CLAP_FACTORIES)
		if (strcmp(k.name, name) == 0) return &k;
	return nullptr;
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
	std::vector<std::string> fragments;
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
			if (std::string f; read_fragment(t, f)) {
				fragments.push_back(f);
				continue;
			}
			// one hop through a GOT slot (exported factory in PIC)
			if (ea_t p; !is_fn_table(t, probe) && load_ptr_const(t, p)) t = p;
			if (is_fn_table(t, probe)) tables.push_back(t);
		}
	}

	if (ids.empty() && !fragments.empty()) {
		if (const id_type_t* k = fragment_id(fragments)) {
			rv_log("get_factory at %a: %s matched from %u id fragment(s)",
				gf_ea, k->name, unsigned(fragments.size()));
			ids.push_back(k);
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

		const id_type_t* kind = by_id(CLAP_FACTORIES, r.id.c_str());
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
		if (!resolve_table(ea, tif)) {
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