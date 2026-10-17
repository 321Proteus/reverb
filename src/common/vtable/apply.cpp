#include "common/vtable/apply.hpp"
#include "common/vtable/layout.hpp"
#include "util.hpp"
#include <bytes.hpp>
#include <funcs.hpp>
#include <name.hpp>
#include <nalt.hpp>

namespace rv {

tinfo_t find_class_type(const qstring& cls) {
	tinfo_t t;
	if (cls.empty()) return t;

	auto usable = [](const tinfo_t& x) {
		return x.is_struct() && x.get_size() != BADSIZE;
	};
	if (t.get_named_type(nullptr, cls.c_str()) && usable(t)) return t;

	// Unqualified fallback only when the short name is unambiguous
	const qstring s = rtti::short_name(cls);
	if (s == cls) return tinfo_t();

	int matches = 0;
	const uint32 lim = get_ordinal_limit();
	for (uint32 ord = 1; ord < lim && matches < 2; ord++) {
		const char* n = get_numbered_type_name(nullptr, ord);
		if (n != nullptr && rtti::short_name(n) == s) matches++;
	}

	if (matches == 1 && t.get_named_type(nullptr, s.c_str()) && usable(t))
		return t;
	return tinfo_t();
}

} // namespace rv

namespace rv::vtbl {

static bool set_shared_cmt(ea_t f, int refs) {
	qstring cmt, out, line;
	line.sprnt("shared by %d vtable slots", refs);
	func_t* func = get_func(f);

	get_func_cmt(&cmt, func, true);

	size_t start = 0;
	while (start <= cmt.length()) {
		size_t end = cmt.find('\n', start);
		if (end == qstring::npos) end = cmt.length();

		const qstring l = cmt.substr(start, end);
		if (!l.empty() &&
			!(l.starts_with("shared by ") && l.ends_with(" vtable slots"))) {
			if (!out.empty()) out.append('\n');
			out.append(l);
		}
		start = end + 1;
	}
	if (!out.empty()) out.append('\n');
	out.append(line);

	return out != cmt && set_func_cmt(func, out.c_str(), true);
}

static tinfo_t slot_prototype(
	const slot_rec& s, const func_rec& fr, const label_policy& p) {
	tinfo_t ft = s.type.is_ptr() ? s.type.get_pointed_object() : s.type;
	if (!ft.is_func() || fr.kind == func_kind::THUNK) return ft;

	// Thunks and a this inside the owner keep the interface this
	const tinfo_t cls = find_class_type(fr.owner);
	if (cls.empty() || (fr.this_off != 0 && !p.use_shifted_this)) return ft;

	func_type_data_t ftd;
	if (!ft.get_func_details(&ftd) || ftd.empty()) return ft;

	tinfo_t self;
	if (fr.this_off == 0) {
		self.create_ptr(cls);
	} else {
		ptr_type_data_t pt(tinfo_t(), 0, cls, int32(fr.this_off));
		pt.obj_type = cls;
		self.create_ptr(pt);
	}
	if (self.empty()) return ft;

	ftd[0].type = self; // argloc and __hidden stay
	tinfo_t out;
	return out.create_func(ftd) ? out : ft;
}

label_stats apply(const model& m, const label_policy& p) {
	label_stats st;
	std::set<ea_t> failed;

	for (const table_rec& r : m.vtables) {
		if (r.iface_vtbl.empty()) continue;

		tinfo_t cur;
		if (get_tinfo(&cur, r.ea) && cur.equals_to(r.iface_vtbl)) continue;

		del_items(r.ea, DELIT_EXPAND, r.iface_vtbl.get_size());
		if (apply_tinfo(r.ea, r.iface_vtbl, TINFO_DEFINITE)) st.vtables++;
		else
			failed.insert(r.ea);
	}

	for (const auto& [f, fr] : m.funcs) {

		switch (fr.kind) {
		case func_kind::PURECALL:
			st.purecall++;
			continue;
		case func_kind::SKIP:
			st.skipped++;
			continue;
		case func_kind::INHERITED:
			st.inherited++;
			break;
		case func_kind::FOLDED:
			st.folded++;
			break;
		case func_kind::THUNK:
			st.thunks++;
			break;
		default:
			break;
		}

		const slot_rec* s = fr.rec >= 0 ? &m.slots[fr.rec] : nullptr;
		if (s != nullptr && failed.count(s->vt) != 0) {
			st.skipped++;
			continue;
		}

		if (get_func(f) == nullptr && is_slot_target(f)) add_func(f);
		func_t* pf = get_func(f);
		if (pf == nullptr || pf->start_ea != f || (pf->flags & FUNC_LIB) != 0) {
			st.skipped++;
			continue;
		}

		if (fr.kind == func_kind::FOLDED) {
			if (p.comment_shared) set_shared_cmt(f, fr.refs);
			continue;
		}
		if (s == nullptr ||
			fr.name.empty()) { // thunk without one owner and method
			st.skipped++;
			continue;
		}

		// Names are unique within the model; one taken outside it is not
		// suffixed
		if (p.rename && has_dummy_name(get_flags(f))) {
			if (set_name(f, fr.name.c_str(), SN_NOWARN)) st.named++;
			else
				st.name_taken++;
		}

		if (p.set_prototypes && !is_userti(f)) {
			const tinfo_t ft = slot_prototype(*s, fr, p);
			if (!ft.empty() && apply_tinfo(f, ft, TINFO_DEFINITE))
				st.prototyped++;
		}
	}

	return st;
}

qstring format_stats(const label_stats& s) {
	qstring out;
	out.sprnt("vtables %d, named %d, prototyped %d, inherited %d, folded %d, "
			  "thunks %d, purecall %d, skipped %d, name taken %d",
		s.vtables, s.named, s.prototyped, s.inherited, s.folded, s.thunks,
		s.purecall, s.skipped, s.name_taken);
	return out;
}

bool label(ea_t vtbl_ea, const char* type_name) {
	model m;
	if (!m.add(vtbl_ea, type_name) || m.vtables.front().iface_vtbl.empty())
		return false;

	m.collect();
	const label_stats st = apply(m);

	// Typed by this or earlier
	tinfo_t cur;
	return st.vtables > 0 || (get_tinfo(&cur, vtbl_ea) &&
								 cur.equals_to(m.vtables.front().iface_vtbl));
}

qstring pick_type(ea_t ea, std::initializer_list<const char*> candidates) {
	const size_t n = extent(ea);
	const size_t psz = ptr_size();

	for (const char* name : candidates) {
		tinfo_t ti;
		if (!ti.get_named_type(nullptr, name)) continue;
		const size_t sz = ti.get_size();
		if (sz != BADSIZE && sz / psz <= n) return name;
	}
	return qstring();
}

} // namespace rv::vtbl
