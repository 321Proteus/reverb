#include "plugin.hpp"
#include "exts.hpp"
#include "factory.hpp"
#include "label.hpp"
#include "core/log.hpp"
#include "db/func_iter.hpp"
#include "db/members.hpp"
#include "db/read.hpp"
#include "db/segments.hpp"
#include "decomp/analysis.hpp"
#include "decomp/stores.hpp"
#include "patterns/id_returns.hpp"
#include <algorithm>
#include <map>
#include <name.hpp>
#include <set>

namespace rv::clap {

static constexpr int MIN_FNS = 4; // callbacks to accept a store-built instance
static constexpr int MAX_FUNCS = 32; // functions walked from create_plugin
static constexpr int MAX_DEPTH = 2;

struct plugin_type_t {
	tinfo_t tif;
	udt_type_data_t udt;
	int64 desc_off = 0;
};

struct plugin_hit_t {
	ea_t func = BADADDR; // function that stores the callbacks
	ea_t ea = BADADDR;	 // static template, when not built by stores
	ea_t cmt_ea = BADADDR;
	const desc_t* desc = nullptr;
	std::map<qstring, ea_t> fns; // member -> callback
};

static const desc_t* desc_at(const std::vector<desc_t>& descs, ea_t ea) {
	for (const desc_t& d : descs) {
		if (d.ea == ea) return &d;
	}

	return nullptr;
}

static bool is_callback(ea_t ea) {
	return is_code_addr(ea) && !in_extern_seg(ea);
}

// The instance at var + base, from the pointers stored into its members
static plugin_hit_t from_stores(const plugin_type_t& pt,
	const std::vector<store_t>& stores, int var, int64 base,
	const std::vector<desc_t>& descs) {

	plugin_hit_t h;

	for (const store_t& s : stores) {
		if (s.loc.var != var || s.is_or || s.val.t != val_t::ADDR ||
			s.size < ptr_size())
			continue;

		const int64 off = s.loc.off - base;

		if (off == pt.desc_off) {
			if ((h.desc = desc_at(descs, s.val.ea)) != nullptr) h.cmt_ea = s.ea;
			continue;
		}

		for (const udm_t& m : pt.udt) {
			if (!m.type.is_funcptr() || int64(m.offset / 8) != off ||
				!is_callback(s.val.ea))
				continue;

			h.fns[m.name] = s.val.ea;
			if (h.cmt_ea == BADADDR) h.cmt_ea = s.ea;
		}
	}

	return h;
}

static void add_hit(std::vector<plugin_hit_t>& out, plugin_hit_t&& h) {
	auto it = std::find_if(out.begin(), out.end(),
		[&](const plugin_hit_t& o) { return o.fns == h.fns; });

	if (it == out.end()) out.push_back(std::move(h));

	else if (it->desc == nullptr)
		h.desc = it->desc;
}

/*
	Instances built in cf: at a store of a known descriptor, and with shape
	set, at the base where the most callbacks land (a base one slot off
	still matches all but one)
*/
static void scan_func(cfunc_t* cf, const plugin_type_t& pt,
	const std::vector<desc_t>& descs, bool shape,
	std::vector<plugin_hit_t>& out) {

	store_collector_t col(cf);
	col.apply_to(&cf->body, nullptr);

	std::set<std::pair<int, int64>> anchored, shaped;
	for (const store_t& s : col.stores) {

		if (s.val.t != val_t::ADDR) continue;

		if (desc_at(descs, s.val.ea) != nullptr)
			anchored.insert({ s.loc.var, s.loc.off - pt.desc_off });

		else if (shape && is_callback(s.val.ea)) {
			for (const udm_t& m : pt.udt) {
				if (m.type.is_funcptr())
					shaped.insert({ s.loc.var, s.loc.off - m.offset / 8 });
			}
		}
	}

	for (const auto& [var, base] : anchored) {

		plugin_hit_t h = from_stores(pt, col.stores, var, base, descs);
		if (h.fns.size() < 2) continue;

		h.func = cf->entry_ea;
		add_hit(out, std::move(h));
	}

	std::map<int, plugin_hit_t> best; // per var

	for (const auto& [var, base] : shaped) {

		plugin_hit_t h = from_stores(pt, col.stores, var, base, descs);
		if (int(h.fns.size()) < MIN_FNS) continue;

		auto it = best.find(var);
		if (it == best.end() || h.fns.size() > it->second.fns.size())
			best[var] = std::move(h);
	}

	for (auto& [var, h] : best) {
		h.func = cf->entry_ea;
		add_hit(out, std::move(h));
	}
}

// A static clap_plugin_t the function references
static void static_templates(
	func_t* pfn, const plugin_type_t& pt, std::vector<plugin_hit_t>& out) {
	rv_fii_t fii;

	for (bool ok = rv_fii_set(fii, pfn->start_ea); ok; ok = fii.next_code()) {
		xrefblk_t xb;

		for (bool x = xb.first_from(fii.current(), XREF_DATA); x;
			x = xb.next_from()) {

			ea_t t = xb.to;
			if (!resolve_table(t, pt.tif)) continue;

			plugin_hit_t h;
			h.func = pfn->start_ea;
			h.ea = h.cmt_ea = t;

			for (const udm_t& m : pt.udt) {
				if (m.type.is_funcptr())
					h.fns[m.name] = read_ptr(t + m.offset / 8);
			}

			add_hit(out, std::move(h));
		}
	}
}

// create_plugin and the functions it calls directly
static void walk_calls(ea_t root, const plugin_type_t& pt,
	const std::vector<desc_t>& descs, std::vector<plugin_hit_t>& out) {

	std::vector<std::pair<ea_t, int>> todo{ { root, 0 } };
	std::set<ea_t> seen;

	for (size_t i = 0; i < todo.size() && int(seen.size()) < MAX_FUNCS; i++) {

		const auto [ea, depth] = todo[i];
		if (!seen.insert(ea).second || in_extern_seg(ea)) continue;

		func_t* pfn = rv::ensure_func(ea);
		if (pfn == nullptr || (pfn->flags & FUNC_LIB) != 0) continue;

		cfuncptr_t cf = rv::decompile_at(ea);
		if (cf == nullptr) continue;

		scan_func(cf, pt, descs, true, out);
		static_templates(pfn, pt, out);
		if (depth == MAX_DEPTH) continue;

		literal_collector_t lc;
		lc.apply_to(&cf->body, nullptr);

		for (cexpr_t* c : lc.calls) {
			cexpr_t* fn = strip_casts(c->x);
			if (fn->op == cot_obj && is_code_addr(fn->obj_ea))
				todo.push_back({ rv::resolve_thunk(fn->obj_ea), depth + 1 });
		}
	}
}

static void label_extensions(ea_t ge) {

	struct ext_hit_t {
		ea_t ea;
		const id_type_t* kind;
		bool conditional;
	};
	std::vector<ext_hit_t> hits;

	std::vector<rv::id_return_t> rets = rv::find_id_returns(ge, 1);
	if (rets.empty()) {
		const ea_t impl = rv::direct_tail_callee(ge);
		if (impl != BADADDR) rets = rv::find_id_returns(impl, 1);
	}
	if (rets.empty()) {
		rv_log("get_extension at %a: no id checks found", ge);
		return;
	}

	for (const rv::id_return_t& r : rets) {

		const id_type_t* kind = by_id(CLAP_EXTS, r.id.c_str());
		tinfo_t tif;
		if (kind == nullptr || !tif.get_named_type(nullptr, kind->type)) {
			rv_log("Unknown CLAP extension \"%s\" at %a", r.id.c_str(), r.ea);

			qstring cmt;
			cmt.sprnt("CLAP extension \"%s\" (type unknown)", r.id.c_str());
			set_cmt(r.ea, cmt.c_str(), true);
			continue;
		}

		ea_t ea = r.ea;
		if (!resolve_table(ea, tif)) {
			rv_log("CLAP extension \"%s\" at %a is not a %s", r.id.c_str(),
				r.ea, kind->type);
			continue;
		}

		auto it = std::find_if(hits.begin(), hits.end(),
			[&](const ext_hit_t& h) { return h.ea == ea; });
		if (it == hits.end()) hits.push_back({ ea, kind, r.conditional });
		else {
			it->kind = prefer(it->kind, kind);
			it->conditional = it->conditional && r.conditional;
		}
	}

	for (const ext_hit_t& h : hits) {
		qstring prefix(h.kind->type);
		if (prefix.ends_with("_t")) prefix.remove_last(2);
		label_fn_table(h.ea, h.kind->type, prefix.c_str());

		if (has_dummy_name(get_flags(h.ea)))
			set_name(h.ea, prefix.c_str(), SN_NOWARN | SN_FORCE);

		qstring cmt;
		cmt.sprnt("%s%s", h.kind->id, h.conditional ? " (conditional)" : "");
		set_cmt(h.ea, cmt.c_str(), true);

		rv_log("Extension %s (\"%s\") at %a", h.kind->name, cmt.c_str(), h.ea);
	}

	mark_cfunc_dirty(ge);
}

static void label_hit(
	const plugin_hit_t& h, const plugin_type_t& pt, const char* prefix) {

	if (h.ea != BADADDR) {
		label_fn_table(h.ea, "clap_plugin_t", prefix);
		if (has_dummy_name(get_flags(h.ea)))
			set_name(h.ea, prefix, SN_NOWARN | SN_FORCE);

	} else if (func_t* pfn = get_func(h.func)) {

		for (const udm_t& m : pt.udt) {
			auto it = h.fns.find(m.name);
			if (it != h.fns.end())
				label_fn(it->second, m, prefix, pfn->start_ea, pfn->size());
		}

		mark_cfunc_dirty(h.func);
	}

	qstring cmt = "REverb: clap_plugin_t";
	if (h.desc != nullptr) cmt.cat_sprnt(" for \"%s\"", h.desc->id.c_str());

	set_cmt(h.cmt_ea != BADADDR ? h.cmt_ea : h.func, cmt.c_str(), false);

	rv_log("Plugin instance %s from %a (%u callbacks)", prefix, h.func,
		unsigned(h.fns.size()));
}

void label_plugins(ea_t create_plugin_ea, const std::vector<desc_t>& descs) {

	plugin_type_t pt;
	uint64 off;

	if (!pt.tif.get_named_type(nullptr, "clap_plugin_t") ||
		!pt.tif.get_udt_details(&pt.udt) ||
		!find_member(pt.tif, "desc", &off, nullptr))
		return;

	pt.desc_off = int64(off);

	std::vector<plugin_hit_t> hits;
	// Functions that xref a descriptor
	std::set<ea_t> funcs;

	for (const desc_t& d : descs) {
		xrefblk_t xb;
		for (bool ok = xb.first_to(d.ea, XREF_DATA); ok; ok = xb.next_to())
			if (func_t* f = get_func(xb.from)) funcs.insert(f->start_ea);
	}

	for (ea_t f : funcs) {
		if (cfuncptr_t cf = rv::decompile_at(f))
			scan_func(cf, pt, descs, false, hits);
	}

	if (hits.empty() && create_plugin_ea != BADADDR && create_plugin_ea != 0)
		walk_calls(create_plugin_ea, pt, descs, hits);

	if (hits.empty()) {
		rv_log("No clap_plugin_t found from create_plugin at %a",
			create_plugin_ea);
		return;
	}

	std::set<ea_t> exts;
	for (size_t i = 0; i < hits.size(); i++) {

		const plugin_hit_t& h = hits[i];
		qstring prefix = "clap_plugin";

		if (hits.size() > 1) {
			if (h.desc != nullptr) prefix += qstring("_") + c_ident(h.desc->id);
			else
				prefix.cat_sprnt("_%u", unsigned(i));
		}

		label_hit(h, pt, prefix.c_str());

		auto ge = h.fns.find("get_extension");
		if (ge != h.fns.end() && exts.insert(ge->second).second)
			label_extensions(ge->second);
	}
}

} // namespace rv::clap
