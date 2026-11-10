#include "aeffect.hpp"
#include "core/log.hpp"
#include "db/segments.hpp"
#include "db/read.hpp"
#include "decomp/analysis.hpp"
#include "decomp/fields.hpp"
#include "decomp/stores.hpp"
#include "ui/report.hpp"
#include "vst2.hpp"
#include <cstring>
#include <hexrays.hpp>
#include <ida.hpp>
#include <llong.hpp>
#include <pro.h>
#include <segment.hpp>
#include <vector>

namespace rv::vst2 {

namespace {

qstring flags_str(uint32 f) {
	static const struct {
		uint32 bit;
		const char* name;
	} kFlags[] = {
		{ 1u << 0, "HasEditor" },
		{ 1u << 4, "CanReplacing" },
		{ 1u << 5, "ProgramChunks" },
		{ 1u << 8, "IsSynth" },
		{ 1u << 9, "NoSoundInStop" },
		{ 1u << 10, "ExtIsAsync" },
		{ 1u << 11, "ExtHasBuffer" },
		{ 1u << 12, "CanDoubleReplacing" },
	};
	qstring s;
	for (const auto& k : kFlags)
		if (f & k.bit) {
			if (!s.empty()) s += " | ";
			s += k.name;
		}
	return s.empty() ? qstring("none") : s;
}

bool type_exists(const char* name) {
	tinfo_t t;
	return t.get_named_type(get_idati(), name);
}

// Callback prototypes from aeffect.h
qstring proto_for(const char* field) {
	const bool is64 = inf_is_64bit();
	const char* ip =
		type_exists("VstIntPtr") ? "VstIntPtr" : (is64 ? "__int64" : "int");

	// clang-format off
	const char* i32 = type_exists("VstInt32") 	? "VstInt32"  : "int";
	const char* op  = type_exists("VstOpcode") ? "VstOpcode" : i32;
	const char* eff = type_exists("AEffect") 	? "AEffect *" : "void *";
	// clang-format on

	qstring d;
	if (!strcmp(field, "dispatcher"))
		d.sprnt("%s __cdecl f(%seffect, %s opcode, %s index, %s value, void "
				"*ptr, float opt);",
			ip, eff, op, i32, ip);

	else if (!strcmp(field, "process") || !strcmp(field, "processReplacing"))
		d.sprnt("void __cdecl f(%seffect, float **inputs, float **outputs, %s "
				"sampleFrames);",
			eff, i32);

	else if (!strcmp(field, "processDoubleReplacing"))
		d.sprnt("void __cdecl f(%seffect, double **inputs, double **outputs, "
				"%s sampleFrames);",
			eff, i32);

	else if (!strcmp(field, "setParameter"))
		d.sprnt(
			"void __cdecl f(%seffect, %s index, float parameter);", eff, i32);

	else if (!strcmp(field, "getParameter"))
		d.sprnt("float __cdecl f(%seffect, %s index);", eff, i32);

	return d;
}

} // namespace

std::vector<aeffect_hit_t> extract_aeffects(
	const std::vector<aeffect_field_t>& fields, int aeffect_size, cfunc_t* cf) {

	std::vector<aeffect_hit_t> hits;
	if (cf == nullptr) return hits;

	store_collector_t col(cf);
	col.apply_to(&cf->body, nullptr);

	rv_log("AEffect scan %a: %u foldable stores, %u aliases", cf->entry_ea,
		unsigned(col.stores.size()), unsigned(col.alias.size()));

	std::vector<std::pair<int, int64>> seen; // dedup (root, offset)

	for (const store_t& m : col.stores) {

		if (m.is_or || m.val.t != val_t::NUM || m.size < 4) continue;
		if (uint32(m.val.num) != VST2_MAGIC) continue;

		std::pair<int, int64> key{ m.loc.var, m.loc.off };
		if (std::find(seen.begin(), seen.end(), key) != seen.end()) continue;
		seen.push_back(key);

		aeffect_hit_t h{};
		h.func = cf->entry_ea;
		h.magic_ea = m.ea;
		h.root = m.loc.var;
		h.base_off = m.loc.off;

		qstring root_name =
			(m.loc.var >= 0 && size_t(m.loc.var) < cf->get_lvars()->size())
				? cf->get_lvars()->at(m.loc.var).name
				: qstring("?");

		rv_log("  magic store at %a: AEffect at %s%+lld", m.ea,
			root_name.c_str(), (long long)m.loc.off);

		for (const store_t& s : col.stores) {

			if (s.loc.var == h.root && s.loc.off + s.size > h.base_off &&
				s.loc.off < h.base_off + aeffect_size) {

				rv_log("    store +%lld [%d]  %s",
					(long long)(s.loc.off - h.base_off), s.size,
					s.text.c_str());
			}
		}

		resolve_fields(
			fields, h, col.stores, inf_is_64bit(), "this (wrapper object)");

		// AEffect needs a dispatcher pointing at code. This rejects FourCC
		// collisions and anything that merely looks like the magic.
		auto d = h.fields.find("dispatcher");
		if (d == h.fields.end() || d->second.v.t != val_t::ADDR) {
			rv_log("  -> rejected: dispatcher not set to a constant address");
			continue;
		}

		if (!is_code_addr(d->second.v.ea)) {
			rv_log(
				"  -> rejected: dispatcher %a is not in an executable segment",
				d->second.v.ea);
			continue;
		}

		rv_log("  -> accepted AEffect (dispatcher %a)", d->second.v.ea);
		hits.push_back(std::move(h));
	}

	if (hits.empty() && !seen.empty())
		rv_log("AEffect scan %a: %u magic store(s), none validated",
			cf->entry_ea, unsigned(seen.size()));

	return hits;
}

void apply_aeffect(const std::vector<aeffect_field_t>& fields,
	const aeffect_hit_t& h, bool multiple) {

	auto num = [&](const char* n, uint64* out) -> bool {
		auto it = h.fields.find(n);
		if (it == h.fields.end() || it->second.v.t != val_t::NUM) return false;

		*out = it->second.v.num;
		return true;
	};

	uint64 uid = 0;
	const bool have_uid = num("uniqueID", &uid);

	// Shell plugins carry several AEffects, suffix names with the uniqueID
	qstring suffix;
	if (multiple && have_uid) suffix.sprnt("_%08X", uint32(uid));

	rv_log("Applying AEffect from %a%s", h.func,
		have_uid ? (qstring(" uniqueID ") + fourcc(uint32(uid))).c_str() : "");

	// One function can back several fields (e.g. a shared no-op for process and
	// processDoubleReplacing); name it once, comment the other roles.
	std::map<ea_t, std::string> named;

	for (const aeffect_field_t& f : fields) {

		if (f.kind != F_FN) continue;
		auto it = h.fields.find(f.name);
		if (it == h.fields.end() || it->second.v.t != val_t::ADDR) continue;

		const ea_t ea = it->second.v.ea;

		if (auto prev = named.find(ea); prev != named.end()) {
			qstring c;
			c.sprnt("also AEffect::%s", f.name.c_str());
			append_cmt(ea, c.c_str(), false);

			rv_log("    %s -> %a (same as %s, commented)", f.name.c_str(), ea,
				prev->second.c_str());
			continue;
		}

		// On-demand analysis: turns &unk_X into code + function.
		if (ensure_func(ea) == nullptr) {
			rv_log("    %s -> %a: could not create a function, skipping",
				f.name.c_str(), ea);
			continue;
		}

		// Never clobber real symbols (debug info, exports, user renames)
		qstring nm;
		nm.sprnt("vst2_%s%s", f.name.c_str(), suffix.c_str());

		if (has_user_name(get_flags(ea))) {
			qstring cur;
			get_ea_name(&cur, ea);
			rv_log("    %s -> %a: keeping existing name '%s'", f.name.c_str(),
				ea, cur.c_str());

		} else if (set_name(ea, nm.c_str(), SN_NOWARN | SN_FORCE)) {
			rv_log("    %s -> %a: named %s", f.name.c_str(), ea, nm.c_str());
		}

		// Don't override a user-defined prototype
		if (!is_userti(ea)) {
			if (apply_definite_proto(ea, proto_for(f.name.c_str()).c_str()))
				rv_log("    %s -> %a: prototype applied", f.name.c_str(), ea);
		}

		named[ea] = f.name.c_str();
		mark_cfunc_dirty(ea);
	}

	qstring c = "REverb: AEffect initializer (VST2)";
	if (have_uid) {
		uint64 ver = 0;
		c.cat_sprnt("\nuniqueID %s", fourcc(uint32(uid)).c_str());
		if (num("version", &ver))
			c.cat_sprnt(", version %llu", (unsigned long long)ver);
	}
	uint64 ni = 0, no = 0, np = 0, pr = 0, fl = 0;
	const bool have_in = num("numInputs", &ni),
			   have_out = num("numOutputs", &no);

	if (have_in || have_out)
		c.cat_sprnt("\nI/O: %s in / %s out",
			have_in ? std::to_string(ni).c_str() : "?",
			have_out ? std::to_string(no).c_str() : "?");

	if (num("numParams", &np))
		c.cat_sprnt("\nparams: %llu", (unsigned long long)np);

	if (num("numPrograms", &pr))
		c.cat_sprnt(", programs: %llu", (unsigned long long)pr);

	const bool have_flags = num("flags", &fl);
	uint32 or_flags = 0;
	if (auto ob = h.or_bits.find("flags"); ob != h.or_bits.end())
		or_flags = uint32(ob->second);

	const uint32 flags = uint32(fl) | or_flags;

	c.cat_sprnt("\nflags%s: %s", have_flags ? "" : " (at least)",
		flags_str(flags).c_str());
	c.cat_sprnt("\nAEffect at offset +0x%llX in the wrapper object",
		(unsigned long long)h.base_off);

	const ea_t cmt_ea = (h.magic_ea != BADADDR) ? h.magic_ea : h.func;
	set_cmt(cmt_ea, c.c_str(), false);
	mark_cfunc_dirty(h.func);

	rv_log("  %s", c.c_str());

	report::heading("VST2 AEffect", cmt_ea);

	if (have_uid) report::row("uniqueID", fourcc(uint32(uid)));
	if (uint64 ver = 0; num("version", &ver))
		report::row("version", qstring(std::to_string(ver).c_str()));

	if (have_in || have_out) {
		qstring io;
		io.sprnt("%s in / %s out", have_in ? std::to_string(ni).c_str() : "?",
			have_out ? std::to_string(no).c_str() : "?");
		report::row("I/O", io);
	}

	if (num("numParams", &np))
		report::row("params", qstring(std::to_string(np).c_str()));
	if (num("numPrograms", &pr))
		report::row("programs", qstring(std::to_string(pr).c_str()));

	report::row(have_flags ? "flags" : "flags (at least)", flags_str(flags));
	report::addr_row("built by", h.func);

	for (const aeffect_field_t& f : fields) {
		if (f.kind != F_FN) continue;

		auto it = h.fields.find(f.name);
		if (it != h.fields.end() && it->second.v.t == val_t::ADDR)
			report::addr_row(f.name, it->second.v.ea);
	}
}

} // namespace rv::vst2