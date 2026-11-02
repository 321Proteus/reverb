#include "descriptor.hpp"
#include "core/log.hpp"
#include "db/segments.hpp"
#include "db/read.hpp"
#include "db/types.hpp"
#include "decomp/analysis.hpp"
#include "db/func_iter.hpp"
#include "db/members.hpp"
#include <name.hpp>
#include <set>

namespace rv::clap {

static constexpr int MAX_FEATURES = 64;

// NULL or a valid string
static bool read_opt_str(
	ea_t base, const tinfo_t& tif, const char* field, qstring* out = nullptr) {
	const ea_t p = member_ptr(base, tif, field);
	qstring s;
	if (p != 0 && !read_utf8(p, s)) return false;
	if (out != nullptr) *out = s;
	return true;
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

bool read_descriptor(ea_t ea, const tinfo_t& tif, desc_t& out) {
	const size_t sz = tif.get_size();
	if (sz == BADSIZE || !is_loaded(ea) || !is_loaded(ea + sz - 1))
		return false;

	abi::clap_version_t v;
	const ea_t v_ea = member_ea(ea, tif, "clap_version");
	if (v_ea == BADADDR || get_bytes(&v, sizeof(v), v_ea) != ssize_t(sizeof(v)))
		return false;
	if (v.major != 1 || v.minor > 64 || v.revision > 1024) return false;

	desc_t d;
	d.ea = ea;
	d.clap_version = v;

	if (!read_utf8(member_ptr(ea, tif, "id"), d.id) || d.id.empty() ||
		!read_utf8(member_ptr(ea, tif, "name"), d.name) || d.name.empty())
		return false;

	if (!read_opt_str(ea, tif, "vendor", &d.vendor) ||
		!read_opt_str(ea, tif, "url") || !read_opt_str(ea, tif, "manual_url") ||
		!read_opt_str(ea, tif, "support_url") ||
		!read_opt_str(ea, tif, "version", &d.version) ||
		!read_opt_str(ea, tif, "description"))
		return false;

	const ea_t fp = member_ptr(ea, tif, "features");
	if (fp != 0) {
		if (!read_features(fp, d.features)) return false;
		d.features_ea = fp;
	}

	out = std::move(d);
	return true;
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

static qstring c_ident(const qstring& s) {
	qstring r = s;
	for (char& c : r)
		if (uchar(c) >= 0x80 || !qisalnum(c)) c = '_';
	return r;
}

static const char* or_none(const qstring& s) {
	return s.empty() ? "(none)" : s.c_str();
}

void label_descriptor(const desc_t& d) {
	if (!apply_type_at(d.ea, "clap_plugin_descriptor_t")) return;

	const qstring base = qstring("clap_desc_") + c_ident(d.id);
	if (has_dummy_name(get_flags(d.ea)))
		set_name(d.ea, base.c_str(), SN_NOWARN | SN_FORCE);

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
	set_cmt(d.ea, c.c_str(), true);

	rv_log("Descriptor %s (\"%s\") at %a", d.id.c_str(), d.name.c_str(), d.ea);
}

} // namespace rv::clap
