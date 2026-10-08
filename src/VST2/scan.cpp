#include "VST2/opcode.hpp"
#include "common/analysis.hpp"
#include "aeffect.hpp"
#include "vst2.hpp"
#include "util.hpp"
#include <auto.hpp>
#include <funcs.hpp>
#include <hexrays.hpp>
#include <ida.hpp>
#include <name.hpp>
#include <pro.h>
#include <search.hpp>
#include <bytes.hpp>
#include <segment.hpp>
#include <set>
#include <ua.hpp>

#if IDA_SDK_VERSION >= 940

typedef function_item_iterator_t rv_fii_t;

static ea_t rv_func_start(int i) { return get_func_ea_by_num(i); }
static bool rv_fii_set(rv_fii_t& it, ea_t ea) { return it.set(ea); }

#else

typedef func_item_iterator_t rv_fii_t;

static ea_t rv_func_start(int i) {
	func_t* f = getn_func(i);
	return f ? f->start_ea : BADADDR;
}

static bool rv_fii_set(rv_fii_t& it, ea_t ea) {
	func_t* f = get_func(ea);
	return f && it.set(f);
}

#endif

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

static bool load_aeffect_layout(
	std::vector<aeffect_field_t>& out, int& total_size) {
	tinfo_t t;
	if (!t.get_named_type(get_idati(), "AEffect") || !t.is_struct()) {
		rv_log("AEffect type not found in local types - was types.hpp parsed?");
		return false;
	}

	size_t sz = t.get_size();
	const int expected = inf_is_64bit() ? 192 : 144;
	if (sz == BADSIZE || int(sz) != expected) {
		rv_log(
			"AEffect is %d bytes, expected %d - check types.hpp declarations",
			sz == BADSIZE ? -1 : int(sz), expected);
		return false;
	}
	total_size = int(sz);

	for (const char* name : members) {
		udm_t udm;
		udm.name = name;
		if (t.find_udm(&udm, STRMEM_NAME) < 0) {
			rv_log("AEffect::%s missing from types.hpp, skipping", name);
			continue;
		}
		fkind_t k = udm.type.is_funcptr() ? F_FN
					: udm.type.is_ptr()	  ? F_PTR
										  : F_INT;
		out.push_back({ name, int(udm.offset / 8), int(udm.size / 8),
			k }); // udm is in bits

		rv_log("  layout: %-22s +%-3d size %d %s", name, int(udm.offset / 8),
			int(udm.size / 8),
			k == F_FN	 ? "fn"
			: k == F_PTR ? "ptr"
						 : "int");
	}
	return !out.empty();
}

void scan() {

	std::set<ea_t> candidates;
	find_candidates(candidates);

	std::vector<aeffect_hit_t> all_hits;

	std::vector<aeffect_field_t> fields;
	int size;
	load_aeffect_layout(fields, size);

	for (const ea_t c_ea : candidates) {

		func_t* f = ensure_func(c_ea);
		if (f) {
			hexrays_failure_t hf;
			cfuncptr_t cf = decompile(f, &hf, DECOMP_NO_WAIT);

			if (!cf) {
				rv_log("Failed to decompile %a: %s", f->start_ea,
					hf.desc().c_str());
			} else {
				auto hits = extract_aeffects(fields, size, cf);
				all_hits.insert(all_hits.end(), hits.begin(), hits.end());
			}
		} else {
			rv_log("No function at candidate %a", c_ea);
		}
	}

	for (const aeffect_hit_t& h : all_hits) {
		apply_aeffect(fields, h, all_hits.size() > 1);

		auto d = h.fields.find("dispatcher");
		dispatcher_info_t info;
		if (d != h.fields.end() && analyze_dispatcher(d->second.v.ea, info))
			apply_dispatcher(info);
	}
}
} // namespace rv::vst2