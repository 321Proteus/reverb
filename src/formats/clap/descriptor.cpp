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
#include <name.hpp>
#include <set>

namespace rv::clap {

static constexpr int MAX_FEATURES = 64;
static constexpr int MAX_INIT_FUNCS = 16;
static constexpr int CELL_SCAN = 256; // bytes of a lazy cell searched for
									  // its init function

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

	for (const auto& [var, base] : bases) {
		auto rd = [&](int64 off, int size, uint64& v) {
			return stored_const(col.stores, var, base + off, size, v);
		};

		desc_t d;
		d.init_ea = fn;
		if (!parse_descriptor(tif, rd, d)) continue;

		auto same = [&](const desc_t& o) { return o.id == d.id; };
		if (std::none_of(out.begin(), out.end(), same))
			out.push_back(std::move(d));
	}
}

// The one zero-filled, descriptor-sized object get_plugin_descriptor takes
// the address of: storage that the init function fills in
static ea_t zero_storage(ea_t gpd_ea, size_t sz) {
	cfuncptr_t cf = rv::decompile_at(gpd_ea);
	if (cf == nullptr) return BADADDR;

	struct ref_finder_t : public ctree_visitor_t {
		size_t sz;
		std::set<ea_t> found;

		ref_finder_t(size_t s) : ctree_visitor_t(CV_FAST), sz(s) {}

		int idaapi visit_expr(cexpr_t* e) override {
			if (e->op != cot_ref) return 0;
			cexpr_t* x = strip_casts(e->x);
			if (x->op != cot_obj || !in_data_seg(x->obj_ea)) return 0;

			const ea_t ea = x->obj_ea;
			if (!is_loaded(ea) || !is_loaded(ea + sz - 1)) return 0;
			for (size_t i = 0; i < sz; i++)
				if (get_byte(ea + i) != 0) return 0;
			found.insert(ea);
			return 0;
		}
	};

	ref_finder_t rf(sz);
	rf.apply_to(&cf->body, nullptr);
	return rf.found.size() == 1 ? *rf.found.begin() : BADADDR;
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
}

} // namespace rv::clap
