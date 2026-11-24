#include "extensions.hpp"
#include "core/log.hpp"
#include "decomp/analysis.hpp"
#include "formats/fn_table.hpp"
#include "patterns/id_returns.hpp"
#include "ui/report.hpp"
#include <algorithm>
#include <name.hpp>

namespace rv::lv2 {

ext_scan_t find_extensions(ea_t ed) {
	ext_scan_t s;
	s.fn = ed;

	if (uint64 n; rv::const_return(ed, &n) && n == 0) {
		rv_log("extension_data at %a returns no extensions", ed);
		return s;
	}

	std::vector<rv::id_return_t> rets = rv::find_id_returns(ed, 0);
	if (rets.empty()) {
		const ea_t impl = rv::direct_tail_callee(ed);
		if (impl != BADADDR) rets = rv::find_id_returns(impl, 0);
	}

	if (rets.empty()) {
		rv_log("extension_data at %a: no URI checks found", ed);
		s.status = ext_scan_t::NO_CHECKS;
		return s;
	}

	s.status = ext_scan_t::FOUND;
	for (const rv::id_return_t& r : rets) {

		ext_t e;
		e.uri = r.id;
		e.ea = r.ea;
		e.conditional = r.conditional;
		e.kind = by_uri(EXTENSIONS, r.id.c_str());

		tinfo_t tif;
		if (e.kind == nullptr || !tif.get_named_type(nullptr, e.kind->type)) {
			rv_log("Unknown LV2 extension \"%s\" at %a", r.id.c_str(), r.ea);
			e.kind = nullptr;
			s.exts.push_back(e);
			continue;
		}

		if (!resolve_table(e.ea, tif, true)) {
			rv_log("LV2 extension \"%s\" at %a is not a %s", r.id.c_str(), r.ea,
				e.kind->type);
			continue;
		}

		// Two URIs of one table, or one URI checked twice
		auto it = std::find_if(s.exts.begin(), s.exts.end(),
			[&](const ext_t& o) { return o.kind != nullptr && o.ea == e.ea; });
		if (it == s.exts.end()) s.exts.push_back(e);
		else
			it->conditional = it->conditional && e.conditional;

		rv_log("Extension %s (\"%s\") at %a", e.kind->type, r.id.c_str(), e.ea);
	}
	return s;
}

// LV2_State_Interface -> lv2_state_interface
static qstring type_prefix(const char* type) {
	qstring s(type);
	for (size_t i = 0; i < s.length(); i++)
		s[i] = qtolower(s[i]);
	return s;
}

void label_extensions(const ext_scan_t& s) {
	switch (s.status) {
	case ext_scan_t::NONE: report::row("extensions", "(none)", s.fn); return;
	case ext_scan_t::NO_CHECKS:
		report::row("extensions", "(no URI checks found)", s.fn);
		return;
	case ext_scan_t::FOUND: break;
	}

	for (const ext_t& e : s.exts) {
		qstring cmt;

		if (e.kind == nullptr) {
			cmt.sprnt("LV2 extension \"%s\" (type unknown)", e.uri.c_str());
			set_cmt(e.ea, cmt.c_str(), true);
			report::row("extension", e.uri + " (unknown type)", e.ea);
			continue;
		}

		const qstring prefix = type_prefix(e.kind->type);
		label_fn_table(e.ea, e.kind->type, prefix.c_str());

		if (has_dummy_name(get_flags(e.ea)))
			set_name(e.ea, prefix.c_str(), SN_NOWARN | SN_FORCE);

		cmt.sprnt("%s%s", e.uri.c_str(), e.conditional ? " (conditional)" : "");
		set_cmt(e.ea, cmt.c_str(), true);

		report::row("extension", cmt, e.ea);
	}

	mark_cfunc_dirty(s.fn);
}

} // namespace rv::lv2
