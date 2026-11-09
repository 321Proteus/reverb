#include "descriptor.hpp"
#include "label.hpp"
#include "core/log.hpp"
#include "db/segments.hpp"
#include "db/read.hpp"
#include "db/types.hpp"
#include "decomp/analysis.hpp"
#include "decomp/stores.hpp"
#include "db/func_iter.hpp"
#include "db/members.hpp"
#include <functional>
#include <map>
#include <name.hpp>
#include <set>

namespace rv::clap {

static constexpr int MAX_FEATURES = 64;
static constexpr int MAX_INIT_FUNCS = 16;
static constexpr int CELL_SCAN = 256; // bytes of a lazy cell searched for
									  // its init function

static layout_t desc_layout() {
	layout_t layout;
	load_layout("clap_plugin_descriptor_t", layout);
	return layout;
}

// NULL-terminated array of strings
static bool read_features(ea_t ea, qstrvec_t& out) {
	for (int i = 0; i <= MAX_FEATURES; i++) {
		const ea_t slot = ea + i * ptr_size();
		if (!is_loaded(slot)) return false;

		const ea_t s = read_ptr(slot);
		if (s == 0) return true;

		qstring f;
		if (i == MAX_FEATURES || !read_utf8(s, f)) return false;
		out.push_back(f);
	}
	return false;
}

// Reads size bytes at a byte offset into the descriptor
typedef std::function<bool(int64 off, int size, uint64& out)> field_reader_t;

static bool parse_descriptor(
	const tinfo_t& tif, const field_reader_t& rd, desc_t& d) {

	// Checks are ordered cheapest first
	auto num = [&](const char* path, int size, uint64& v) {
		uint64 off;
		return find_member(tif, path, &off, nullptr) && rd(off, size, v);
	};

	uint64 major, minor, rev;
	if (!num("clap_version.major", 4, major) || major != 1 ||
		!num("clap_version.minor", 4, minor) || minor > 64 ||
		!num("clap_version.revision", 4, rev) || rev > 1024)
		return false;

	d.clap_version = { uint32(major), uint32(minor), uint32(rev) };

	// NULL (unknown counts as NULL) or a valid string
	auto str = [&](const char* path, qstring* out, bool required) {
		uint64 p;
		if (!num(path, ptr_size(), p)) p = 0;

		qstring s;
		if (p == 0) return !required;
		if (!read_utf8(ea_t(p), s) || (required && s.empty())) return false;
		if (out != nullptr) *out = s;
		return true;
	};

	if (!str("id", &d.id, true) || !str("name", &d.name, true) ||
		!str("vendor", &d.vendor, false) || !str("url", nullptr, false) ||
		!str("manual_url", nullptr, false) ||
		!str("support_url", nullptr, false) ||
		!str("version", &d.version, false) ||
		!str("description", nullptr, false))

		return false;

	uint64 fp;
	if (num("features", ptr_size(), fp) && fp != 0) {
		if (!read_features(ea_t(fp), d.features)) return false;
		d.features_ea = ea_t(fp);
	}

	return true;
}

bool read_descriptor(ea_t ea, const tinfo_t& tif, desc_t& out) {
	const size_t sz = tif.get_size();
	if (sz == BADSIZE || !is_loaded(ea) || !is_loaded(ea + sz - 1))
		return false;

	// Pointers are read with read_ptr: scanned data has no offset items
	auto rd = [&](int64 off, int size, uint64& v) {
		v = size == 4 ? get_dword(ea + off) : read_ptr(ea + off);
		return true;
	};

	desc_t d;
	d.ea = ea;
	if (!parse_descriptor(tif, rd, d)) return false;
	out = std::move(d);
	return true;
}

/*
	Functions that may fill a descriptor in: get_plugin_descriptor itself (a
	local static initialized inline), its direct callees, and the init
	functions in the data it references (a Rust lazy static keeps its init
	function in the cell, which get_plugin_descriptor reaches through the GOT)
*/
static std::vector<ea_t> init_candidates(ea_t gpd_ea) {
	std::vector<ea_t> out{ gpd_ea };
	auto add = [&](ea_t ea) {
		if (is_code_addr(ea) && !in_extern_seg(ea) &&
			std::find(out.begin(), out.end(), ea) == out.end() &&
			int(out.size()) < MAX_INIT_FUNCS)
			out.push_back(ea);
	};

	if (cfuncptr_t cf = rv::decompile_at(gpd_ea)) {
		literal_collector_t lc;
		lc.apply_to(&cf->body, nullptr);
		for (cexpr_t* c : lc.calls) {
			cexpr_t* fn = strip_casts(c->x);
			if (fn->op == cot_obj) add(rv::resolve_thunk(fn->obj_ea));
		}
	}

	func_t* pfn = get_func(gpd_ea);
	if (pfn == nullptr) return out;

	const int ps = ptr_size();
	rv_fii_t fii;
	for (bool ok = rv_fii_set(fii, pfn->start_ea); ok; ok = fii.next_code()) {
		xrefblk_t xb;
		for (bool x = xb.first_from(fii.current(), XREF_DATA); x;
			x = xb.next_from()) {
			ea_t t = xb.to;
			if (ea_t p; load_ptr_const(t, p)) t = p;
			if (!in_data_seg(t)) continue;

			for (ea_t a = t; a < t + CELL_SCAN && is_loaded(a + ps - 1);
				a += ps)
				add(read_ptr(a));
		}
	}
	return out;
}

// Descriptors built by stores in fn, found at their id store
static void from_stores(ea_t fn, const tinfo_t& tif, std::vector<desc_t>& out) {
	cfuncptr_t cf = rv::decompile_at(fn);
	uint64 id_off;
	if (cf == nullptr || !find_member(tif, "id", &id_off, nullptr)) return;

	store_collector_t col(cf);
	col.apply_to(&cf->body, nullptr);

	std::set<std::pair<int, int64>> bases;
	for (const store_t& s : col.stores)
		if (s.val.t == val_t::ADDR && s.size == ptr_size() &&
			!is_code_addr(s.val.ea))
			bases.insert({ s.loc.var, s.loc.off - int64(id_off) });

	for (const auto& [v, base] : bases) {
		auto rd = [&](int64 off, int size, uint64& x) {
			return stored_const(col.stores, v, base + off, size, x);
		};

		desc_t d;
		d.init_ea = fn;
		if (!parse_descriptor(tif, rd, d)) continue;

		auto same = [&](const desc_t& o) { return o.id == d.id; };
		if (std::any_of(out.begin(), out.end(), same)) continue;

		const lvars_t* lv = cf->get_lvars();
		rv_log("  clap_plugin_descriptor_t \"%s\" built in %a at %s%+lld",
			d.id.c_str(), fn,
			v >= 0 && size_t(v) < lv->size() ? lv->at(v).name.c_str() : "?",
			base);

		object_fields_t o;
		o.root = v;
		o.base_off = base;

		resolve_fields(desc_layout().fields, o, col.stores, inf_is_64bit());

		out.push_back(std::move(d));
	}
}

static bool zero_filled(ea_t ea, size_t sz) {
	if (!in_data_seg(ea) || !is_loaded(ea + sz - 1)) return false;
	for (size_t i = 0; i < sz; i++)
		if (get_byte(ea + i) != 0) return false;
	return true;
}

/*
	The one zero-filled, descriptor-sized object get_plugin_descriptor returns:
	storage that the init function fills in
*/
static ea_t zero_storage(ea_t gpd_ea, size_t sz) {
	cfuncptr_t cf = rv::decompile_at(gpd_ea);
	if (cf == nullptr) return BADADDR;

	struct ret_finder_t : public ctree_visitor_t {
		std::map<int, ea_t> refs; // v = &obj
		std::set<ea_t> objs;
		std::set<int> vars;

		ret_finder_t() : ctree_visitor_t(CV_FAST) {}

		static ea_t ref_obj(cexpr_t* e) {
			e = strip_casts(e);
			if (e->op != cot_ref) return BADADDR;
			e = strip_casts(e->x);
			return e->op == cot_obj ? e->obj_ea : BADADDR;
		}

		int idaapi visit_expr(cexpr_t* e) override {
			if (e->op != cot_asg) return 0;
			cexpr_t* l = strip_casts(e->x);
			if (l->op == cot_var) refs[l->v.idx] = ref_obj(e->y);
			return 0;
		}

		int idaapi visit_insn(cinsn_t* i) override {
			if (i->op != cit_return) return 0;
			cexpr_t* r = strip_casts(&i->creturn->expr);
			if (r->op == cot_var) vars.insert(r->v.idx);
			else if (const ea_t ea = ref_obj(r); ea != BADADDR)
				objs.insert(ea);
			return 0;
		}
	};

	ret_finder_t rf;
	rf.apply_to(&cf->body, nullptr);

	for (int v : rf.vars) {
		if (auto it = rf.refs.find(v); it != rf.refs.end())
			rf.objs.insert(it->second);
	}

	ea_t found = BADADDR;

	for (ea_t ea : rf.objs) {
		if (ea == BADADDR || !zero_filled(ea, sz)) continue;
		if (found != BADADDR) return BADADDR; // ambiguous

		found = ea;
	}

	return found;
}

std::vector<desc_t> find_descriptors(ea_t gpd_ea) {
	tinfo_t tif;
	if (!tif.get_named_type(nullptr, "clap_plugin_descriptor_t")) return {};

	std::vector<desc_t> out;
	std::set<ea_t> seen;
	auto try_at = [&](ea_t ea) {
		desc_t d;
		if (seen.insert(ea).second && read_descriptor(ea, tif, d))
			out.push_back(std::move(d));
	};

	// return &desc
	if (func_t* pfn = rv::ensure_func(gpd_ea)) {
		rv_fii_t fii;
		for (bool ok = rv_fii_set(fii, pfn->start_ea); ok;
			ok = fii.next_code()) {
			xrefblk_t xb;
			for (bool x = xb.first_from(fii.current(), XREF_DATA); x;
				x = xb.next_from())
				try_at(xb.to);
		}
	}
	if (!out.empty()) return out;

	// Filled in at runtime from constants (lazy or local static)
	for (ea_t fn : init_candidates(gpd_ea))
		from_stores(fn, tif, out);
	if (!out.empty()) {
		rv_log("get_plugin_descriptor at %a: %u descriptor(s) built by code",
			gpd_ea, unsigned(out.size()));
		if (out.size() == 1) out[0].ea = zero_storage(gpd_ea, tif.get_size());
		return out;
	}

	// Built at runtime (e.g. a std::vector), scan the data for static ones
	rv_log("get_plugin_descriptor at %a references no descriptor, scanning "
		   "data",
		gpd_ea);
	const int ps = ptr_size();
	for (segment_t* s = get_first_seg(); s != nullptr;
		s = get_next_seg(s->start_ea)) {
		if (!is_data_seg(s)) continue;

		for (ea_t ea = align_up(s->start_ea, ps); ea + ps <= s->end_ea;
			ea += ps)
			if (get_dword(ea) == 1) try_at(ea); // clap_version.major
	}
	return out;
}

static const char* or_none(const qstring& s) {
	return s.empty() ? "(none)" : s.c_str();
}

void label_descriptor(const desc_t& d) {
	const qstring base = qstring("clap_desc_") + c_ident(d.id);

	qstring features;
	for (const qstring& f : d.features) {
		if (!features.empty()) features += ", ";
		features += f;
	}

	if (d.features_ea != BADADDR) {
		// const char *[n + 1]
		tinfo_t chr(BTF_CHAR);
		chr.set_const();
		tinfo_t ptr, arr;
		ptr.create_ptr(chr);
		arr.create_array(ptr, uint32(d.features.size() + 1));

		del_items(d.features_ea, DELIT_SIMPLE, arr.get_size());
		apply_tinfo(d.features_ea, arr, TINFO_DEFINITE);
		if (has_dummy_name(get_flags(d.features_ea)))
			set_name(d.features_ea, (base + "_features").c_str(),
				SN_NOWARN | SN_FORCE);
	}

	qstring c;
	c.sprnt("CLAP plugin \"%s\"\nid: %s\nvendor: %s\nversion: %s\n"
			"CLAP %u.%u.%u\nfeatures: %s",
		d.name.c_str(), d.id.c_str(), or_none(d.vendor), or_none(d.version),
		d.clap_version.major, d.clap_version.minor, d.clap_version.revision,
		or_none(features));

	if (d.ea != BADADDR && apply_type_at(d.ea, "clap_plugin_descriptor_t")) {
		if (has_dummy_name(get_flags(d.ea)))
			set_name(d.ea, base.c_str(), SN_NOWARN | SN_FORCE);
		set_cmt(d.ea, c.c_str(), true);
	}

	if (func_t* f = d.init_ea != BADADDR ? get_func(d.init_ea) : nullptr) {
		if (has_dummy_name(get_flags(d.init_ea)))
			set_name(d.init_ea, (base + "_init").c_str(), SN_NOWARN | SN_FORCE);
		set_func_cmt(f, c.c_str(), true);
		mark_cfunc_dirty(d.init_ea);
	}

	rv_log("Descriptor %s (\"%s\") at %a", d.id.c_str(), d.name.c_str(),
		d.ea != BADADDR ? d.ea : d.init_ea);

	if (d.ea != BADADDR && d.init_ea == BADADDR) {
		object_fields_t o;
		resolve_data_fields(desc_layout().fields, d.ea, o, inf_is_64bit());
	}
}

} // namespace rv::clap
