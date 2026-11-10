#include "core/log.hpp"
#include "opcode.hpp"
#include "decomp/analysis.hpp"
#include "cxx/vtable.hpp"
#include "aeffect.hpp"
#include "vst2.hpp"
#include <auto.hpp>
#include <funcs.hpp>
#include <hexrays.hpp>
#include <ida.hpp>
#include <nalt.hpp>
#include <name.hpp>
#include <pro.h>
#include <search.hpp>
#include <bytes.hpp>
#include <segment.hpp>
#include <map>
#include <set>
#include <utility>
#include <ua.hpp>
#include "db/func_iter.hpp"
#include "db/types.hpp"
#include "ui/report.hpp"

// Calls cb(ea) for every aligned occurrence of x in the loaded
// image, excluding matches that sit inside instructions (find_imm already
// covers those).
template <typename F>
static void iterate_magic_dword(F&& cb, uint32 x = VST2_MAGIC) {

	uchar b[4];
	for (int i = 0; i < 4; i++)
		b[i] = uchar(x >> (inf_is_be() ? 24 - 8 * i : 8 * i));

	qstring pat;
	pat.sprnt("%02X %02X %02X %02X", b[0], b[1], b[2], b[3]);

	compiled_binpat_vec_t bp;
	qstring err;

	if (!parse_binpat_str(
			&bp, inf_get_min_ea(), pat.c_str(), 16, PBSENC_DEF1BPU, &err)) {
		rv_log("Failed to compile VST2 magic pattern: %s", err.c_str());
		return;
	}

	for (segment_t* s = get_first_seg(); s != nullptr;
		s = get_next_seg(s->start_ea)) {
		if (s->type == SEG_XTRN || s->type == SEG_BSS) continue;

		for (ea_t ea = s->start_ea; ea < s->end_ea; ea++) {
			ea = bin_search(ea, s->end_ea, bp,
				BIN_SEARCH_FORWARD | BIN_SEARCH_NOBREAK | BIN_SEARCH_NOSHOW);

			if (ea == BADADDR) break;
			if (ea % 4 != 0 || is_code(get_flags(get_item_head(ea)))) continue;

			cb(ea);
		}
	}
}

static void find_candidates(std::set<ea_t>& funcs) {

	char buf[256];
	get_input_file_path(buf, sizeof(buf));

	rv_log("waiting for analysis of %s", buf);
	auto_wait();

	const bool arm64 = (inf_get_procname() == "arm") && inf_is_64bit();
	int hits = 0;

	for (int i = 0, n = get_func_qty(); i < n; i++) {

		ea_t func_ea = rv_func_start(i);
		if (func_ea == BADADDR) continue;
		rv_fii_t fii;

		for (bool ok = rv_fii_set(fii, func_ea); ok; ok = fii.next_code()) {

			ea_t ea = fii.current();
			insn_t insn;

			if (decode_insn(&insn, ea) <= 0) continue;
			for (const op_t& op : insn.ops) {

				if (op.type == o_void) break;
				if (op.type != o_imm) continue;

				if (uint32(op.value) == VST2_MAGIC) {
					funcs.insert(func_ea);
					rv_log("Magic imm at %a (func %a)", ea, func_ea);
					hits++;
				}

				else if (arm64 && uint32(op.value) == VST2_MAGIC >> 16) {

					qstring mnem;
					print_insn_mnem(&mnem, ea);
					if (mnem == "MOVK") {
						funcs.insert(func_ea);
						rv_log("Magic MOVK half at %a (func %a)", ea, func_ea);
						hits++;
					}
				}
			}
		}
	}

	iterate_magic_dword([&](ea_t d) {
		xrefblk_t xb;
		for (bool ok = xb.first_to(d, XREF_DATA); ok; ok = xb.next_to())
			if (func_t* f = get_func(xb.from)) funcs.insert(f->start_ea);
	});
}

namespace rv::vst2 {

/*
	Most derived vtables of the object behind an AEffect. When the AEffect
	initializer stores its own object as AEffect::object, it is a constructor
   and the classes built on top of it are the plugin classes. Otherwise follow
   audioMaster from the entry point.
*/
static std::vector<ea_t> plugin_vtables(const aeffect_hit_t& h) {
	auto o = h.fields.find("object");

	if (o != h.fields.end() && o->second.v.t == val_t::LOCAL &&
		o->second.v.loc.var == h.root && o->second.v.loc.off == 0) {

		cfuncptr_t cf = decompile_at(h.func);

		if (cf != nullptr && !cf->argidx.empty() && cf->argidx[0] == h.root) {
			std::vector<ea_t> v = vtbl::most_derived(h.func);
			for (const ea_t vt : v)
				rv_log("Most derived from ctor %a: vtable %a", h.func, vt);
			if (!v.empty()) return v;
		}
	}

	const ea_t vt = find_plugin_vtable(find_entry());
	if (vt == BADADDR) return {};

	return { vt };
}

void scan() {

	const ea_t entry = find_entry();
	report::heading("VST2 entry", entry);
	report::addr_row("entry", entry);

	std::set<ea_t> candidates;
	find_candidates(candidates);

	std::vector<aeffect_hit_t> all_hits;

	layout_t layout;
	load_layout("AEffect", members, layout, inf_is_64bit() ? 192 : 144, false);
	const std::vector<aeffect_field_t>& fields = layout.fields;
	const int size = layout.size;

	for (const ea_t c_ea : candidates) {
		cfuncptr_t cf = decompile_at(c_ea);
		if (cf != nullptr) {
			auto hits = extract_aeffects(fields, size, cf);
			all_hits.insert(all_hits.end(), hits.begin(), hits.end());
		}
	}

	if (all_hits.empty()) {
		report::heading("VST2 AEffect");
		report::row("AEffect", "(not found)");
	}

	int64 object_off = -1;
	for (const aeffect_field_t& f : fields)
		if (f.name == "object") object_off = f.off;

	// Some plugins share a single dispatcher between classes
	std::map<ea_t, std::vector<dispatcher_info_t>> results;
	std::set<std::pair<ea_t, ea_t>> done; // (dispatcher, vtable)

	for (const aeffect_hit_t& h : all_hits) {
		apply_aeffect(fields, h, all_hits.size() > 1);

		auto d = h.fields.find("dispatcher");
		if (d == h.fields.end() || d->second.v.t != val_t::ADDR) continue;
		const ea_t disp = d->second.v.ea;

		std::vector<ea_t> vtables = plugin_vtables(h);
		if (vtables.empty()) vtables.push_back(BADADDR); // direct only

		for (const ea_t vt : vtables) {
			if (!done.insert({ disp, vt }).second) continue;
			rv_log("Plugin object vtable %a (%s)", vt,
				vt == BADADDR ? "none" : rtti::class_name(vt).c_str());

			dispatcher_info_t info;
			if (analyze_dispatcher(disp, vt, object_off, info))
				results[disp].push_back(std::move(info));
		}
	}

	for (const auto& [disp, infos] : results)
		apply_dispatcher(infos);
}
} // namespace rv::vst2
