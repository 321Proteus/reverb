#include "features.hpp"
#include "uris.hpp"
#include "core/log.hpp"
#include "db/read.hpp"
#include "decomp/analysis.hpp"
#include "decomp/stores.hpp"
#include "formats/fn_table.hpp"
#include "patterns/id_returns.hpp"
#include "ui/report.hpp"
#include <algorithm>
#include <map>
#include <name.hpp>

namespace rv::lv2 {

qstrvec_t find_features(ea_t inst) {
	qstrvec_t found;

	for (ea_t fn : { inst, rv::direct_tail_callee(inst) }) {
		if (fn == BADADDR) continue;

		cfuncptr_t cf = rv::decompile_at(fn);
		if (cf == nullptr) continue;

		literal_collector_t lc;
		lc.apply_to(&cf->body, nullptr);
		for (const qstring& s : lc.strs)
			if (by_uri(FEATURES, s.c_str()) != nullptr &&
				std::find(found.begin(), found.end(), s) == found.end())
				found.push_back(s);
	}

	rv_log("instantiate at %a checks %u known feature(s)", inst,
		unsigned(found.size()));

	return found;
}

// p->URI (off 0) or p->data (off ptr_size) of an LV2_Feature *
static bool feature_member(const cexpr_t* e, uint32 off) {
	if (e->op != cot_memptr || e->m != off) return false;

	tinfo_t t = e->x->type;
	if (t.is_ptr()) t = t.get_pointed_object();

	qstring n;
	return t.get_type_name(&n) && n == "LV2_Feature";
}

// The statement itself, or the last one of a block
static cinsn_t* last_stmt(cinsn_t* i) {
	if (i != nullptr && i->op == cit_block)
		i = i->cblock->empty() ? nullptr : &i->cblock->back();
	return i;
}

// A branch that leaves: what follows the if runs only when it was not taken
static bool exits(cinsn_t* i) {
	i = last_stmt(i);
	return i != nullptr && (i->op == cit_break || i->op == cit_continue ||
							   i->op == cit_goto || i->op == cit_return);
}

// Where one feature's data was stored
struct feature_store_t {
	qstring uri;
	ea_t ea = BADADDR; // the assignment
	enum { LOCAL, GLOBAL, OTHER } dest = OTHER;
	int var = -1;
	ea_t obj = BADADDR;
	qstring text; // the destination as printed
};

/*
	x = f->data, and the URI f->URI was compared with on the way there:
	in the matching branch of an enclosing if, or after an if (cmp) exit;
	that skips the rest of the block for every other URI
*/
struct store_finder_t : public ctree_parentee_t {
	cfunc_t* cf;
	std::set<int> uri_vars; // u = f->URI
	std::vector<feature_store_t> out;

	store_finder_t(cfunc_t* f) : cf(f) {}

	// cmp(f->URI, "...") on the same f as base
	bool on_uri(cexpr_t* call, const cexpr_t* base) const {
		for (carg_t& a : *call->a) {
			const cexpr_t* x = strip_casts(&a);
			if (feature_member(x, 0) && x->x->equal_effect(*base)) return true;
			if (x->op == cot_var && uri_vars.count(x->v.idx) != 0)
				return true;
		}
		return false;
	}

	// The URI the if checks when it matches in branch `taken`, else empty
	qstring if_uri(cinsn_t* i, const cexpr_t* base, bool taken) const {
		bool in_then;
		qstring lit;
		cexpr_t* call = literal_cmp(&i->cif->expr, in_then, lit);
		return call != nullptr && in_then == taken && on_uri(call, base)
				   ? lit
				   : qstring();
	}

	qstring uri_of(const citem_t* item, const cexpr_t* base) const {
		const citem_t* child = item;

		// Innermost first
		for (size_t k = parents.size(); k-- > 0; child = parents[k]) {
			if (parents[k] == nullptr) break; // apply_to's parent of the root
			if (parents[k]->is_expr()) continue;
			cinsn_t* i = (cinsn_t*)parents[k];

			if (i->op == cit_if) {
				const bool then = child == i->cif->ithen;
				const qstring u = if_uri(i, base, then);
				if (!u.empty()) return u;
				continue;
			}
			if (i->op != cit_block) continue;

			// if (cmp(f->URI, "u") != 0) break; ... x = f->data;
			qstring found;
			for (cinsn_t& s : *i->cblock) {
				if (&s == child) break;
				if (s.op != cit_if || s.cif->ielse != nullptr ||
					!exits(s.cif->ithen))
					continue;
				const qstring u = if_uri(&s, base, false);
				if (!u.empty()) found = u;
			}
			if (!found.empty()) return found;
		}
		return {};
	}

	int idaapi visit_expr(cexpr_t* e) override {
		if (e->op != cot_asg) return 0;

		const cexpr_t* l = strip_casts(e->x);
		const cexpr_t* r = strip_casts(e->y);

		if (feature_member(r, 0) && l->op == cot_var) {
			uri_vars.insert(l->v.idx);
			return 0;
		}
		if (!feature_member(r, ptr_size())) return 0;

		feature_store_t s;
		s.uri = uri_of(e, r->x);
		if (s.uri.empty()) return 0;
		s.ea = e->ea;

		if (l->op == cot_var) {
			s.dest = feature_store_t::LOCAL;
			s.var = l->v.idx;
		} else if (l->op == cot_obj) {
			s.dest = feature_store_t::GLOBAL;
			s.obj = l->obj_ea;
		}
		l->print1(&s.text, cf);
		tag_remove(&s.text);

		out.push_back(std::move(s));
		return 0;
	}
};

// urid#map -> urid_map, options#options -> options, ui#parent -> ui_parent
static qstring feature_name(const char* uri) {
	qstring s(uri);
	for (const char* p : { "http://lv2plug.in/ns/ext/",
			 "http://lv2plug.in/ns/extensions/", "http://lv2plug.in/ns/" }) {
		if (s.starts_with(p)) {
			s.remove(0, strlen(p));
			break;
		}
	}

	const size_t hash = s.find('#');
	if (hash != qstring::npos && s.substr(0, hash) == s.substr(hash + 1))
		s.resize(hash);
	return c_ident(s);
}

// What f->data points to, as the destination's type
static bool data_type(const uri_type_t& f, tinfo_t& out) {
	tinfo_t t;
	if (f.type == nullptr || !t.get_named_type(nullptr, f.type)) return false;

	// LV2_Handle, LV2UI_Widget: the data is the value
	if (t.is_ptr()) out = t;
	else
		out.create_ptr(t);
	return true;
}

// -> how the destination reads now, for the report
static qstring apply_store(cfunc_t* cf, const feature_store_t& s,
	const std::map<int, int>& var_uses) {

	const uri_type_t* f = by_uri(FEATURES, s.uri.c_str());
	tinfo_t t;
	if (f == nullptr || !data_type(*f, t)) return s.text;

	const qstring name = feature_name(f->uri);

	if (s.dest == feature_store_t::LOCAL) {
		const lvars_t* lv = cf->get_lvars();
		if (s.var < 0 || size_t(s.var) >= lv->size()) return s.text;

		const lvar_t& v = lv->at(s.var);
		if (var_uses.at(s.var) > 1) {
			rv_log("%a: %s holds several features, left untyped", s.ea,
				v.name.c_str());
			return v.name;
		}
		if (v.has_user_name() || v.has_user_type()) return v.name;

		rv::set_lvar(cf->entry_ea, v, name.c_str(), &t);
		return name;
	}

	if (s.dest == feature_store_t::GLOBAL) {
		if (!has_dummy_name(get_flags(s.obj))) return s.text;

		const qstring gname = qstring("lv2_") + name;
		apply_tinfo(s.obj, t, TINFO_DEFINITE);
		set_name(s.obj, gname.c_str(), SN_NOWARN | SN_FORCE);
		return gname;
	}

	// A member of the instance, typed with it
	return s.text;
}

void label_features(ea_t inst, const qstrvec_t& features) {
	std::map<qstring, qstring> dest; // URI -> where its data went

	if (cfuncptr_t cf = rv::decompile_at(inst)) {
		store_finder_t sf(cf);
		sf.apply_to(&cf->body, nullptr);

		std::map<int, int> var_uses; // local -> distinct URIs stored in it
		std::set<std::pair<int, qstring>> seen;
		for (const feature_store_t& s : sf.out)
			if (s.dest == feature_store_t::LOCAL && seen.insert({ s.var, s.uri }).second)
				var_uses[s.var]++;

		for (const feature_store_t& s : sf.out) {
			const qstring d = apply_store(cf, s, var_uses);
			rv_log("Feature %s stored in %s at %a", s.uri.c_str(), d.c_str(),
				s.ea);
			dest.insert({ s.uri, d });
		}
		if (!sf.out.empty()) mark_cfunc_dirty(inst);
	}

	for (const qstring& uri : features) {
		const uri_type_t* f = by_uri(FEATURES, uri.c_str());
		qstring v = uri;
		if (f != nullptr && f->type != nullptr) v.cat_sprnt(" (%s)", f->type);

		auto it = dest.find(uri);
		if (it != dest.end()) v.cat_sprnt(" -> %s", it->second.c_str());
		report::row("feature", v, inst);
	}
}

} // namespace rv::lv2
