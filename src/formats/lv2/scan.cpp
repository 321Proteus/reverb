#include "lv2.hpp"
#include "descriptor.hpp"
#include "features.hpp"
#include "core/log.hpp"
#include "db/func_iter.hpp"
#include "db/members.hpp"
#include "decomp/analysis.hpp"
#include "formats/fn_table.hpp"
#include "ui/report.hpp"
#include <name.hpp>
#include <set>

namespace rv::lv2 {

struct entry_t {
	const char* name;
	const char* proto; // LV2_SYMBOL_EXPORT: the default calling convention
};

static const entry_t DESCRIPTOR = { "lv2_descriptor",
	"const LV2_Descriptor *lv2_descriptor(uint32_t index);" };
static const entry_t LIB_DESCRIPTOR = { "lv2_lib_descriptor",
	"const LV2_Lib_Descriptor *lv2_lib_descriptor(const char *bundle_path, "
	"const LV2_Feature *const *features);" };
static const entry_t UI_DESCRIPTOR = { "lv2ui_descriptor",
	"const LV2UI_Descriptor *lv2ui_descriptor(uint32_t index);" };

// What scan found, for label
struct state_t {
	bool valid = false;
	ea_t desc = BADADDR, lib = BADADDR, ui = BADADDR; // the exports
	ea_t lib_table = BADADDR; // the static LV2_Lib_Descriptor
	std::vector<desc_t> plugins, uis;
};

static state_t g_state;

static ea_t find_entry(const entry_t& e) {
	const ea_t ea = get_name_ea(BADADDR, e.name);
	return ea != BADADDR
			   ? ea
			   : get_name_ea(BADADDR, (qstring("_") + e.name).c_str());
}

// The static LV2_Lib_Descriptor that lv2_lib_descriptor returns
static ea_t find_lib_table(ea_t entry) {
	tinfo_t tif;
	func_t* pfn = rv::ensure_func(entry);
	if (pfn == nullptr || !tif.get_named_type(nullptr, "LV2_Lib_Descriptor"))
		return BADADDR;

	rv_fii_t fii;
	for (bool ok = rv_fii_set(fii, pfn->start_ea); ok; ok = fii.next_code()) {
		xrefblk_t xb;
		for (bool x = xb.first_from(fii.current(), XREF_DATA); x;
			x = xb.next_from()) {
			ea_t t = xb.to;
			if (resolve_table(t, tif, true) &&
				member_ptr(t, tif, "get_plugin") != 0) {
				rv_log("LV2_Lib_Descriptor at %a", t);
				return t;
			}
		}
	}

	rv_log(
		"lv2_lib_descriptor at %a returns no static LV2_Lib_Descriptor", entry);
	return BADADDR;
}

/*
	Descriptors from getter, with what their instantiate and extension_data
	check for
*/
static std::vector<desc_t> scan_descriptors(
	ea_t getter, const desc_kind_t& kind, std::set<ea_t>& done) {

	std::vector<desc_t> descs = find_descriptors(getter, kind);
	if (descs.empty()) rv_log("No %s found from %a", kind.type, getter);

	tinfo_t tif;
	if (!tif.get_named_type(nullptr, kind.type)) return descs;

	for (desc_t& d : descs) {
		rv_log("LV2 %s \"%s\" at %a", kind.label, d.uri.c_str(), d.ea);

		const ea_t inst = member_ptr(d.ea, tif, "instantiate");
		d.features = find_features(inst);

		const ea_t ed = member_ptr(d.ea, tif, "extension_data");
		if (ed == 0 || ed == BADADDR) continue;

		d.exts.fn = ed;
		d.exts_shared = !done.insert(ed).second;
		if (!d.exts_shared) d.exts = find_extensions(ed);
	}
	return descs;
}

void scan() {
	g_state = {};

	tinfo_t probe; // lookup only
	if (!probe.get_named_type(nullptr, "LV2_Descriptor")) {
		rv_log("LV2 types are not loaded (install the formats type library)");
		return;
	}

	state_t& s = g_state;
	s.desc = find_entry(DESCRIPTOR);
	s.lib = find_entry(LIB_DESCRIPTOR);
	s.ui = find_entry(UI_DESCRIPTOR);
	if (s.desc == BADADDR && s.lib == BADADDR && s.ui == BADADDR) {
		rv_log("LV2 entry not found");
		return;
	}

	std::set<ea_t> done;
	if (s.desc != BADADDR) s.plugins = scan_descriptors(s.desc, PLUGIN, done);

	// A library descriptor is an alternative to lv2_descriptor
	if (s.lib != BADADDR) s.lib_table = find_lib_table(s.lib);
	if (s.lib_table != BADADDR) {
		const ea_t gp =
			member_ptr(s.lib_table, "LV2_Lib_Descriptor", "get_plugin");
		std::vector<desc_t> more = scan_descriptors(gp, PLUGIN, done);
		s.plugins.insert(s.plugins.end(), more.begin(), more.end());
	}

	if (s.ui != BADADDR) s.uis = scan_descriptors(s.ui, UI, done);
	s.valid = true;
}

static void label_entry(const entry_t& e, ea_t ea) {
	if (ea == BADADDR || rv::ensure_func(ea) == nullptr) return;
	if (rv::apply_definite_proto(ea, e.proto)) mark_cfunc_dirty(ea);
}

static void label_lib_table(ea_t t) {
	tinfo_t tif;
	if (!tif.get_named_type(nullptr, "LV2_Lib_Descriptor")) return;

	label_fn_table(t, "LV2_Lib_Descriptor", "lv2_lib");
	if (has_dummy_name(get_flags(t)))
		set_name(t, "lv2_lib_desc", SN_NOWARN | SN_FORCE);

	report::heading("LV2 library descriptor", t);
	for (const char* m : { "cleanup", "get_plugin" })
		report::addr_row(m, member_ptr(t, tif, m));
}

static void label_descriptors(
	const std::vector<desc_t>& descs, const desc_kind_t& kind, ea_t getter) {

	if (getter == BADADDR) return;

	if (descs.empty()) {
		qstring title;
		title.sprnt("LV2 %s", kind.label);
		report::heading(title);
		report::row(kind.type, "(not found)");
		report::addr_row("returned by", getter);
		return;
	}

	for (const desc_t& d : descs)
		label_descriptor(d, descs.size() == 1);
	mark_cfunc_dirty(getter);
}

void label() {
	const state_t& s = g_state;
	if (!s.valid) return;

	label_entry(DESCRIPTOR, s.desc);
	label_entry(LIB_DESCRIPTOR, s.lib);
	label_entry(UI_DESCRIPTOR, s.ui);

	report::heading("LV2 entry points");
	report::addr_row(DESCRIPTOR.name, s.desc);
	report::addr_row(LIB_DESCRIPTOR.name, s.lib);
	report::addr_row(UI_DESCRIPTOR.name, s.ui);

	if (s.lib_table != BADADDR) label_lib_table(s.lib_table);

	label_descriptors(s.plugins, PLUGIN,
		s.desc != BADADDR ? s.desc
		: s.lib_table != BADADDR
			? member_ptr(s.lib_table, "LV2_Lib_Descriptor", "get_plugin")
			: BADADDR);
	label_descriptors(s.uis, UI, s.ui);
}

} // namespace rv::lv2
