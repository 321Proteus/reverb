#include "common/analysis.hpp"
#include "common/stores.hpp"
#include "vst2.hpp"
#include "aeffect.hpp"
#include <cstring>
#include <hexrays.hpp>
#include <ida.hpp>
#include <llong.hpp>
#include <pro.h>
#include <segment.hpp>
#include <util.hpp>
#include <vector>

namespace rv::vst2 {

namespace {

qstring val_str(const val_t& v, const aeffect_hit_t& h) {
	qstring s;
	switch (v.t) {
	case val_t::NUM:
		s.sprnt("0x%llX", (unsigned long long)v.num);
		break;
	case val_t::ADDR: {
		qstring n;
		get_ea_name(&n, v.ea);
		s.sprnt("%s (%a)", n.empty() ? "?" : n.c_str(), v.ea);
		break;
	}
	case val_t::LOCAL:
		if (v.loc.var == h.root && v.loc.off == 0) s = "this (wrapper object)";
		else
			s.sprnt("var%d%+lld", v.loc.var, (long long)v.loc.off);
		break;
	default:
		s = "<runtime value>";
		break;
	}
	return s;
}

// Extract <= 8 bytes located rel bytes into a NUM store
static uint64 slice_num(const store_t& s, int64 rel, int fsz) {
	uint64 x = 0;
	if (s.val.raw_len > 0) {
		// Constant read from memory: slice the raw bytes (works for 16-byte
		// stores)
		for (int i = 0; i < fsz && rel + i < s.val.raw_len; i++)
			x |= uint64(s.val.raw[rel + i]) << (8 * i);
	} else {
		// Immediate: at most 8 bytes in num; anything beyond is zero
		// (e.g. *(_OWORD *)p = 0)
		const int64 shift = rel * 8;
		x = (shift < 64) ? (s.val.num >> shift) : 0;
		if (fsz < 8) x &= (1ULL << (fsz * 8)) - 1;
	}
	return x;
}

void resolve_fields(const std::vector<aeffect_field_t>& fields,
	aeffect_hit_t& h, const std::vector<store_t>& stores, bool is64) {

	const int psz = is64 ? 8 : 4;

	for (const aeffect_field_t& f : fields) {
		const int64 foff = h.base_off + f.off;
		const int fsz = f.sz;

		for (const store_t& s : stores) {

			if (s.loc.var != h.root) continue;

			// accumulate instead of overwriting
			if (s.is_or) {
				if (s.val.t == val_t::NUM && strcmp(f.name, "flags") == 0 &&
					s.loc.off >= foff && s.loc.off + s.size <= foff + fsz)
					h.flags |= uint32(s.val.num) << (8 * (s.loc.off - foff));
				continue;
			}

			// Does this store fully cover the field?
			if (foff < s.loc.off || foff + fsz > s.loc.off + s.size) continue;

			const int64 rel = foff - s.loc.off;
			val_t v = s.val;

			if (v.t == val_t::NUM) {
				v.num = slice_num(s, rel, fsz);
				v.raw_len = 0; // field value is scalar now

				// Zeroing must not erase a real value written earlier
				if (v.num == 0 && h.fields.count(f.name)) continue;
			} else if (v.t == val_t::ADDR || v.t == val_t::LOCAL) {
				// Pointers: exactly at the field, full width
				if (rel != 0 || s.size < psz) continue;
			}

			h.fields[f.name] = { v, s.ea };
		}

		auto it = h.fields.find(f.name);
		if (it == h.fields.end())
			rv_log("    %-22s (+%3d)  <not written here>", f.name, f.off);
		else
			rv_log("    %-22s (+%3d)  %s   [store at %a]", f.name, f.off,
				val_str(it->second.v, h).c_str(), it->second.src_ea);
	}
}

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

// Callback prototypes from aeffect.h. Uses the SDK typedefs when your VST2
// types.hpp defines them, otherwise equivalent plain types.
qstring proto_for(const char* field) {
	const bool is64 = inf_is_64bit();
	const char* ip =
		type_exists("VstIntPtr") ? "VstIntPtr" : (is64 ? "__int64" : "int");
	const char* i32 = type_exists("VstInt32") ? "VstInt32" : "int";
	const char* op = type_exists("VstOpcode") ? "VstOpcode" : i32;
	const char* eff = type_exists("AEffect") ? "AEffect *" : "void *";

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

		for (const store_t& s : col.stores)
			if (s.loc.var == h.root && s.loc.off + s.size > h.base_off &&
				s.loc.off < h.base_off + aeffect_size)
				rv_log("    store +%lld [%d]  %s",
					(long long)(s.loc.off - h.base_off), s.size,
					s.text.c_str());

		resolve_fields(fields, h, col.stores, inf_is_64bit());

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
			c.sprnt("also AEffect::%s", f.name);
			append_cmt(ea, c.c_str(), false);
			rv_log("    %s -> %a (same as %s, commented)", f.name, ea,
				prev->second.c_str());
			continue;
		}

		// On-demand analysis: turns &unk_X into code + function.
		if (ensure_func(ea) == nullptr) {
			rv_log("    %s -> %a: could not create a function, skipping",
				f.name, ea);
			continue;
		}

		// Never clobber real symbols (debug info, exports, user renames).
		qstring nm;
		nm.sprnt("vst2_%s%s", f.name, suffix.c_str());
		if (has_user_name(get_flags(ea))) {
			qstring cur;
			get_ea_name(&cur, ea);
			rv_log("    %s -> %a: keeping existing name '%s'", f.name, ea,
				cur.c_str());
		} else if (set_name(ea, nm.c_str(), SN_NOWARN | SN_FORCE)) {
			rv_log("    %s -> %a: named %s", f.name, ea, nm.c_str());
		}

		// Same for types: don't override a prototype someone already set.
		if (!is_userti(ea)) {
			if (apply_definite_proto(ea, proto_for(f.name).c_str()))
				rv_log("    %s -> %a: prototype applied", f.name, ea);
		}

		named[ea] = f.name;
		mark_cfunc_dirty(ea); // stale pseudocode otherwise (bit us before)
	}

	// ---- summary comment on the magic store
	// ------------------------------------
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
	const uint32 flags = uint32(fl) | h.flags;
	c.cat_sprnt("\nflags%s: %s", have_flags ? "" : " (at least)",
		flags_str(flags).c_str());
	c.cat_sprnt("\nAEffect at offset +0x%llX in the wrapper object",
		(unsigned long long)h.base_off);

	const ea_t cmt_ea = (h.magic_ea != BADADDR) ? h.magic_ea : h.func;
	set_cmt(cmt_ea, c.c_str(), false);
	mark_cfunc_dirty(h.func);

	rv_log("  %s", c.c_str());
}

} // namespace rv::vst2