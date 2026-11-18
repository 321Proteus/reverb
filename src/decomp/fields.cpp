#include "fields.hpp"
#include "db/read.hpp"
#include <algorithm>
#include <bytes.hpp>
#include <pro.h>

namespace rv {

qstring val_str(
	const val_t& v, const object_fields_t& o, const char* self_label) {
	qstring s;
	switch (v.t) {

	case val_t::NUM: {
		if (v.raw_len == 0) {
			s.sprnt("0x%llX", (unsigned long long)v.num);
			break;
		}

		for (int i = 0; i < v.raw_len; i++)
			s.cat_sprnt("%02X", v.raw[i]);
		break;
	}

	case val_t::ADDR: {
		qstring n;
		get_ea_name(&n, v.ea);
		s.sprnt("%s (%a)", n.empty() ? "?" : n.c_str(), v.ea);
		break;
	}

	case val_t::LOCAL:
		if (v.loc.var == o.root && v.loc.off == 0) s = self_label;
		else
			s.sprnt("var%d%+lld", v.loc.var, (long long)v.loc.off);
		break;

	case val_t::STR: s.sprnt("\"%s\"", v.str.c_str()); break;

	default:		 s = "<runtime value>"; break;
	}

	return s;
}

static qstring field_str(const field_desc_t& f, const val_t& v,
	const object_fields_t& o, const char* self_label) {
	qstring str, s;

	if ((f.kind == F_STR || f.kind == F_CSTR) && v.t == val_t::ADDR &&
		read_utf8(v.ea, str)) {
		s.sprnt("\"%s\"", str.c_str());
		return s;
	}

	return val_str(v, o, self_label);
}

void resolve_fields(const std::vector<field_desc_t>& fields, object_fields_t& o,
	const std::vector<store_t>& stores, bool is64, const char* self_label) {

	const int psz = is64 ? 8 : 4;

	for (const field_desc_t& f : fields) {
		const int64 foff = o.base_off + f.off;
		const int fsz = f.sz;

		for (const store_t& s : stores) {

			if (s.loc.var != o.root) continue;

			// accumulate instead of overwriting
			if (s.is_or) {
				if (s.val.t == val_t::NUM && f.kind == F_INT &&
					s.loc.off >= foff && s.loc.off + s.size <= foff + fsz)
					o.or_bits[f.name] |= uint64(s.val.num)
										 << (8 * (s.loc.off - foff));
				continue;
			}

			// Skip if the field is not fully written
			if (foff < s.loc.off || foff + fsz > s.loc.off + s.size) continue;

			const int64 rel = foff - s.loc.off;
			val_t v = s.val;

			if (v.t == val_t::NUM && fsz > 8) {
				// Only constants read from memory have bytes this wide
				if (s.val.raw_len < rel + fsz) continue;

				memcpy(v.raw, s.val.raw + rel, fsz);
				v.raw_len = fsz;

			} else if (v.t == val_t::NUM) {
				v.num = slice_num(s, rel, fsz);
				v.raw_len = 0;

				if (v.num == 0 && o.fields.count(f.name)) continue;

			} else if (v.t == val_t::ADDR || v.t == val_t::LOCAL) {
				if (rel != 0 || s.size < psz) continue;
			}

			o.fields[f.name] = { v, s.ea };
		}

		auto it = o.fields.find(f.name);
		if (it == o.fields.end())
			rv_log(
				"    %-22s (+%3d)  <not written here>", f.name.c_str(), f.off);
		else
			rv_log("    %-22s (+%3d)  %s   [store at %a]", f.name.c_str(),
				f.off, field_str(f, it->second.v, o, self_label).c_str(),
				it->second.src_ea);
	}
}

void resolve_data_fields(const std::vector<field_desc_t>& fields, ea_t ea,
	object_fields_t& o, bool is64, const char* self_label) {

	const int psz = is64 ? 8 : 4;
	o.root = 0;
	o.base_off = 0;

	std::vector<store_t> stores;
	for (const field_desc_t& f : fields) {

		const ea_t at = ea + f.off;
		if (f.sz <= 0 || !is_loaded(at) || !is_loaded(at + f.sz - 1)) continue;

		store_t s;
		s.loc = { 0, f.off };
		s.size = f.sz;
		s.ea = at;

		if (f.kind == F_STR) {

			// The characters are in place
			s.val.t = val_t::ADDR;
			s.val.ea = at;
			s.size = std::max(f.sz, psz);

		} else if ((f.kind == F_FN || f.kind == F_PTR || f.kind == F_CSTR) &&
				   f.sz == psz) {

			// Scanned data has no offset items
			const ea_t p = read_ptr(at);
			if (p != 0) {
				s.val.t = val_t::ADDR;
				s.val.ea = p;
			} else {
				s.val.t = val_t::NUM;
			}

		} else if (f.sz <= 8) {
			s.val.t = val_t::NUM;
			for (int i = f.sz - 1; i >= 0; i--) {
				s.val.num = (s.val.num << 8) | get_byte(at + i);
			}

		} else if (f.sz <= int(sizeof(s.val.raw))) {
			s.val.t = val_t::NUM;
			s.val.raw_len = f.sz;
			get_bytes(s.val.raw, f.sz, at);

		} else {
			continue;
		}

		stores.push_back(std::move(s));
	}

	resolve_fields(fields, o, stores, is64, self_label);
}

} // namespace rv