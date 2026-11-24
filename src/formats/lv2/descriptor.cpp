#include "descriptor.hpp"
#include "features.hpp"

#include "core/log.hpp"
#include "db/func_iter.hpp"
#include "db/members.hpp"
#include "db/read.hpp"
#include "db/segments.hpp"
#include "db/types.hpp"
#include "decomp/analysis.hpp"
#include "formats/fn_table.hpp"
#include "ui/report.hpp"
#include <algorithm>
#include <name.hpp>
#include <segment.hpp>

namespace rv::lv2 {

static constexpr int MAX_DESCS = 256; // walked in one array

// A URI is basicly text with a scheme, no whitespace
static bool read_uri(ea_t ea, qstring& out) {
	if (ea == 0 || !read_utf8(ea, out) || out.empty()) return false;

	return strchr(out.c_str(), ':') != nullptr &&
		   std::none_of(out.begin(), out.end(), [](char c) {
			   return c == ' ' || c == '\t' || c == '\n' || c == '\r';
		   });
}

static bool is_callback(ea_t ea) {
	return is_code_addr(ea) && !in_extern_seg(ea);
}

static bool read_descriptor(
	ea_t ea, const tinfo_t& tif, const desc_kind_t& kind, desc_t& out) {

	const size_t sz = tif.get_size();
	if (sz == BADSIZE || !is_loaded(ea) || !is_loaded(ea + sz - 1) ||
		in_got_seg(ea))
		return false;

	// The required callbacks
	for (const char* const* n = kind.need; *n != nullptr; n++)
		if (!is_callback(member_ptr(ea, tif, *n))) return false;

	udt_type_data_t udt;
	if (!tif.get_udt_details(&udt)) return false;

	for (const udm_t& m : udt) {
		if (!m.type.is_funcptr()) continue;

		const ea_t fn = read_ptr(ea + m.offset / 8);
		if (fn != 0 && !is_callback(fn)) return false;
	}

	qstring uri;
	if (!read_uri(member_ptr(ea, tif, "URI"), uri)) return false;

	out.ea = ea;
	out.kind = &kind;
	out.uri = uri;

	return true;
}

// Descriptors the function references (one or an array, values or pointers)
static void from_refs(ea_t fn, const tinfo_t& tif, const desc_kind_t& kind,
	std::vector<desc_t>& out) {

	func_t* pfn = rv::ensure_func(fn);
	if (pfn == nullptr) return;

	auto add = [&](const desc_t& d) {
		auto same = [&](const desc_t& o) { return o.ea == d.ea; };
		if (std::none_of(out.begin(), out.end(), same)) out.push_back(d);
	};

	const size_t sz = tif.get_size();
	const int ps = ptr_size();

	rv_fii_t fii;
	for (bool ok = rv_fii_set(fii, pfn->start_ea); ok; ok = fii.next_code()) {
		xrefblk_t xb;
		for (bool x = xb.first_from(fii.current(), XREF_DATA); x;
			x = xb.next_from()) {

			const ea_t t = xb.to;
			desc_t d;

			if (read_descriptor(t, tif, kind, d)) {
				for (int k = 0;
					k < MAX_DESCS && read_descriptor(t + k * sz, tif, kind, d);
					k++)
					add(d);
				continue;
			}

			for (int k = 0; k < MAX_DESCS && is_loaded(t + k * ps); k++) {
				if (!read_descriptor(read_ptr(t + k * ps), tif, kind, d)) break;
				add(d);
			}
		}
	}
}

// Every static descriptor in the data segments
static void from_data(
	const tinfo_t& tif, const desc_kind_t& kind, std::vector<desc_t>& out) {

	const int ps = ptr_size();
	uint64 inst_off;
	if (!find_member(tif, "instantiate", &inst_off, nullptr)) return;

	for (segment_t* s = get_first_seg(); s != nullptr;
		s = get_next_seg(s->start_ea)) {
		if (!is_data_seg(s)) continue;

		for (ea_t ea = align_up(s->start_ea, ps); ea + ps <= s->end_ea;
			ea += ps) {
			desc_t d;
			if (is_callback(read_ptr(ea + inst_off)) && // cheap filter
				read_descriptor(ea, tif, kind, d))
				out.push_back(d);
		}
	}
}

std::vector<desc_t> find_descriptors(ea_t getter, const desc_kind_t& kind) {
	tinfo_t tif;
	if (!tif.get_named_type(nullptr, kind.type)) return {};

	std::vector<desc_t> out;
	if (getter == BADADDR || getter == 0) return out;

	const ea_t fn = rv::resolve_thunk(getter);
	from_refs(fn, tif, kind, out);

	if (out.empty()) {
		const ea_t impl = rv::direct_tail_callee(fn);
		if (impl != BADADDR) from_refs(impl, tif, kind, out);
	}
	if (!out.empty()) return out;

	rv_log("%a references no static %s, scanning data", getter, kind.type);
	from_data(tif, kind, out);
	return out;
}

// The URI without its scheme, as an identifier
static qstring uri_ident(const qstring& uri) {
	qstring s = uri;
	for (const char* scheme : { "https://", "http://", "urn:" }) {
		if (s.starts_with(scheme)) {
			s.remove(0, strlen(scheme));
			break;
		}
	}
	s = c_ident(s);
	while (!s.empty() && s.last() == '_')
		s.remove_last();

	return s;
}

void label_descriptor(const desc_t& d, bool unique) {
	const desc_kind_t& kind = *d.kind;
	const qstring id = uri_ident(d.uri);

	qstring prefix(kind.prefix);
	if (!unique) prefix += qstring("_") + id;

	label_fn_table(d.ea, kind.type, prefix.c_str());

	if (has_dummy_name(get_flags(d.ea)))
		set_name(d.ea, (qstring(kind.data_name) + "_" + id).c_str(),
			SN_NOWARN | SN_FORCE);

	qstring cmt;
	cmt.sprnt("LV2 %s \"%s\"", kind.label, d.uri.c_str());
	set_cmt(d.ea, cmt.c_str(), true);
	rv_log("Labelled %s at %a", cmt.c_str(), d.ea);

	report::heading(cmt, d.ea);

	tinfo_t tif;
	udt_type_data_t udt;
	if (!tif.get_named_type(nullptr, kind.type) || !tif.get_udt_details(&udt))
		return;

	// Callbacks in member order
	for (const udm_t& m : udt) {
		if (!m.type.is_funcptr()) continue;

		const ea_t fn = read_ptr(d.ea + m.offset / 8);
		if (fn == 0) report::row(m.name, "(NULL)");
		else
			report::addr_row(m.name, fn);
	}

	layout_t layout;
	if (load_layout(kind.type, layout)) {
		object_fields_t o;
		resolve_data_fields(layout.fields, d.ea, o, inf_is_64bit());
	}

	label_features(member_ptr(d.ea, tif, "instantiate"), d.features);
	if (d.has_instance)
		label_instance(d.instance, (prefix + "_instance").c_str());

	if (d.exts.fn == BADADDR) return;

	if (d.exts_shared)
		report::row("extensions", "(shared, see above)", d.exts.fn);
	else
		label_extensions(d.exts);
}

} // namespace rv::lv2
