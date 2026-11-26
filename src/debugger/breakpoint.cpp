#include "breakpoint.hpp"
#include "core/log.hpp"
#include "formats/format.hpp"
#include "frameworks/framework.hpp"
#include "ui/report.hpp"
#include <dbg.hpp>
#include <funcs.hpp>
#include <name.hpp>
#include <pro.h>
#include <vector>

namespace rv::debug {

/**
 * A glob pattern that matches more functions than this is too broad for the
 * binary: its breakpoints are skipped instead of flooding the list
 */
static const size_t kMaxPerPattern = 32;

/** `*` matches any run of characters, `?` any one */
static bool glob_match(const char* p, const char* s) {
	const char *star = nullptr, *resume = nullptr;

	while (*s != '\0') {
		if (*p == '?' || *p == *s) {
			p++;
			s++;
		} else if (*p == '*') {
			star = p++;
			resume = s;
		} else if (star != nullptr) {
			p = star + 1;
			s = ++resume;
		} else {
			return false;
		}
	}

	while (*p == '*')
		p++;

	return *p == '\0';
}

/**
 * The names a pattern is tried against: the stored one, and the demangled one
 * without its argument list
 */
static void func_names(ea_t ea, qstring* raw, qstring* dem) {
	get_ea_name(raw, ea);
	dem->clear();

	if (get_ea_name(dem, ea, GN_DEMANGLED | GN_SHORT) <= 0) return;

	const size_t paren = dem->find('(');
	if (paren != qstring::npos) dem->resize(paren);

	if (*dem == *raw) dem->clear();
}

struct module_bpts_t {
	qstring group; ///< Breakpoint folder, e.g. "/REverb/VST2"
	qstring title; ///< "VST2"
	const bpt_spec_t* specs;
};

/** @brief A single breakpoint to add */
struct hit_t {
	ea_t ea;
	const bpt_spec_t* spec;
};

static std::vector<module_bpts_t> collect_modules(
	InterfaceSet formats, Framework fw) {
	std::vector<module_bpts_t> out;

	formats.for_each([&](Interface iface) {
		const FormatModule* m = get_format_module(iface);

		if (m != nullptr && m->breakpoints != nullptr) {
			const char* n = get_interface_name(iface);
			out.push_back({ qstring("/REverb/") + n, n, m->breakpoints });
		}
	});

	const FrameworkModule* m = get_framework_module(fw);

	if (m != nullptr && m->breakpoints != nullptr) {
		const char* n = get_framework_name(fw);
		out.push_back({ qstring("/REverb/") + n, n, m->breakpoints });
	}

	return out;
}

/** The functions each pattern of @p mod matches, the first pattern winning */
static std::vector<hit_t> match_functions(const module_bpts_t& mod) {
	size_t nspecs = 0;
	while (mod.specs[nspecs].pattern != nullptr)
		nspecs++;

	std::vector<std::vector<ea_t>> per_spec(nspecs);
	qstring raw, dem;

	for (size_t i = 0, n = get_func_qty(); i < n; i++) {
		const func_t* f = getn_func(i);
		if (f == nullptr) continue;

		func_names(f->start_ea, &raw, &dem);

		for (size_t s = 0; s < nspecs; s++) {
			const char* p = mod.specs[s].pattern;

			if (glob_match(p, raw.c_str()) ||
				(!dem.empty() && glob_match(p, dem.c_str()))) {
				per_spec[s].push_back(f->start_ea);
				break;
			}
		}
	}

	std::vector<hit_t> hits;
	for (size_t s = 0; s < nspecs; s++) {

		if (per_spec[s].size() > kMaxPerPattern) {
			rv_log("Breakpoints: '%s' matches %u functions, skipped as too "
				   "broad",
				mod.specs[s].pattern, unsigned(per_spec[s].size()));
			continue;
		}

		for (ea_t ea : per_spec[s])
			hits.push_back({ ea, &mod.specs[s] });
	}

	return hits;
}

void add_breakpoints(InterfaceSet formats, Framework fw) {
	const std::vector<module_bpts_t> mods = collect_modules(formats, fw);
	if (mods.empty()) return;

	report::heading("Breakpoints");

	for (const module_bpts_t& mod : mods) {
		const std::vector<hit_t> hits = match_functions(mod);
		unsigned added = 0, enabled = 0;

		qstring summary;
		struct row_t {
			qstring name, value;
			ea_t ea;
		};

		std::vector<row_t> rows;

		for (const hit_t& h : hits) {
			row_t row{ qstring(), qstring(), h.ea };
			get_ea_name(&row.name, h.ea, GN_DEMANGLED | GN_SHORT);

			if (exist_bpt(h.ea)) { // the user's, or an earlier scan's
				row.value.sprnt("%a (existing)", h.ea);
				rows.push_back(row);
				continue;
			}

			bpt_t b;
			b.set_abs_bpt(h.ea);
			b.type = BPT_SOFT;

			if (!h.spec->enabled) b.flags &= ~BPT_ENABLED;
			if (!add_bpt(b)) {
				rv_log("Breakpoints: couldn't add one at %a", h.ea);
				continue;
			}

			if (!h.spec->enabled) enable_bpt(h.ea, false);

			bpt_location_t loc;
			loc.set_abs_bpt(h.ea);
			set_bptloc_group(loc, mod.group.c_str());

			added++;
			if (h.spec->enabled) enabled++;

			row.value.sprnt("%a (%s)", h.ea, h.spec->enabled ? "on" : "off");
			rows.push_back(row);
		}

		summary.sprnt("%u functions, %u added (%u enabled)",
			unsigned(hits.size()), added, enabled);

		rv_log("Breakpoints %s: %s", mod.group.c_str(), summary.c_str());

		report::group(mod.title, summary);
		for (const row_t& r : rows)
			report::child(r.name, r.value, r.ea);
	}
}

} // namespace rv::debug
