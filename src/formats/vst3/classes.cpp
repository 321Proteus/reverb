/**
 * @file classes.cpp
 * @brief Labeling of VST3 classes: `registerClass` matching, create functions,
 * vtables.
 */
#include "factory.hpp"
#include "core/log.hpp"
#include "cxx/vtable.hpp"
#include "db/read.hpp"
#include "db/type_name.hpp"
#include "db/segments.hpp"
#include "decomp/analysis.hpp"
#include "decomp/call.hpp"
#include "decomp/returns.hpp"
#include "ui/report.hpp"
#include <array>
#include <bytes.hpp>
#include <cstring>
#include <funcs.hpp>
#include <map>
#include <name.hpp>
#include <set>
#include <typeinf.hpp>
#include <vector>

namespace rv::vst3 {

namespace {

/**
 * @brief An interface id: its name and the four longs of its `INLINE_UID`
 */
struct iid_t {
	const char* name;
	uint32 l[4];
};

/**
 * @brief The interface ids that queryInterface is matched against, from
 * `iids.inc`
 */
constexpr iid_t KNOWN_IIDS[] = {
#define RV_IID(n, l1, l2, l3, l4) { #n, { l1, l2, l3, l4 } },
#include "iids.inc"
#undef RV_IID
};

/**
 * @brief A `PClassInfo2` and the `registerClass` call that passes it
 */
struct class_rec_t {
	const factory_struct_t* s = nullptr;
	qstring name, category, cid;
	bool have_flags = false;
	uint64 flags = 0;

	ea_t call_ea = BADADDR; /**< The `registerClass` call */
	ea_t callee = BADADDR;	/**< `registerClass` itself */
	ea_t create = BADADDR;	/**< The create function */
};

/**
 * @brief The class id of @p s as 32 hex digits, empty if it is not constant
 */
qstring cid_hex(const factory_struct_t& s) {
	qstring out;
	auto it = s.fields.find("cid");
	if (it == s.fields.end()) return out;

	const val_t& v = it->second.v;
	uchar raw[16];

	if (v.t == val_t::NUM && v.raw_len == 16) memcpy(raw, v.raw, 16);
	else if (v.t != val_t::ADDR || get_bytes(raw, 16, v.ea) != 16)
		return out;

	for (uchar b : raw)
		out.cat_sprnt("%02X", b);

	return out;
}

/**
 * @brief @p s as an identifier: other characters become single `_`, trimmed at
 * the ends
 */
qstring sanitize(const qstring& s) {
	qstring out;

	for (char ch : s) {
		const bool ok = qisalnum(uchar(ch)) || ch == '_';
		if (ok) out.append(ch);

		else if (!out.empty() && out.last() != '_')
			out.append('_');
	}

	while (!out.empty() && out.last() == '_')
		out.remove_last();

	return out;
}

/**
 * @brief Finds the `registerClass` call that passes @p s: the call that passes
 * @p s and a code address, which is the create function
 *
 * @param calls Calls of the factory function
 * @param s     The `PClassInfo2`
 * @param r     Receives `callee` and `create`
 * @return false if no call matches
 */
bool find_register_call(const std::vector<call_t>& calls,
	const factory_struct_t& s, class_rec_t& r) {

	for (const call_t& c : calls) {
		size_t self = c.args.size();

		for (size_t i = 0; i < c.args.size() && self == c.args.size(); i++) {
			const call_arg_t& a = c.args[i];

			if (s.root == GLOBAL_ROOT
					? a.k == call_arg_t::OBJ && a.ea == ea_t(s.base_off)
					: a.k == call_arg_t::VAR && a.var == s.root)
				self = i;
		}

		if (self == c.args.size()) continue;

		for (size_t i = 0; i < c.args.size(); i++) {
			const call_arg_t& a = c.args[i];

			if (i == self || a.k != call_arg_t::OBJ || !is_code_addr(a.ea) ||
				in_data_seg(a.ea))
				continue;

			const ea_t f = resolve_thunk(a.ea);
			if (ensure_func(f) == nullptr) {
				rv_log(
					"  registerClass call at %a: no function at %a", c.ea, f);
				return false;
			}

			r.call_ea = c.ea;
			r.callee = c.callee;
			r.create = f;

			return true;
		}
	}

	return false;
}

/**
 * @brief Appends @p text to the function comment of @p ea unless it already
 * holds it
 */
void add_func_cmt(ea_t ea, const qstring& text) {
	qstring cur;
	func_t* func = get_func(ea);

	get_func_cmt(&cur, func, false);
	if (cur.find(text) != qstring::npos) return;

	if (!cur.empty()) cur.append('\n');
	cur.append(text);
	set_func_cmt(func, cur.c_str(), false);
}

/**
 * @brief Appends @p text to the comment at @p ea unless it already holds it
 */
void add_cmt(ea_t ea, const qstring& text) {
	qstring cur;
	get_cmt(&cur, ea, false);
	if (cur.find(text) == qstring::npos) append_cmt(ea, text.c_str(), false);
}

/**
 * @brief Names the function @p ea @p nm, keeping a name a user gave it
 */
void name_func(ea_t ea, const qstring& nm) {
	if (has_user_name(get_flags(ea))) {
		qstring cur;
		get_ea_name(&cur, ea);
		rv_log("    %a: keeping existing name '%s'", ea, cur.c_str());
	} else if (set_name(ea, nm.c_str(), SN_NOWARN | SN_FORCE)) {
		rv_log("    %a: named %s", ea, nm.c_str());
	}
}

/**
 * @brief Names the create function of @p r @p fname and refreshes its
 * decompilation
 */
void apply_class(const class_rec_t& r, const qstring& fname) {
	name_func(r.create, fname);

	if (!is_userti(r.create)) {
		qstring d;
		const qstring unk = resolve_type(VST3_SCOPE, "FUnknown");

		d.sprnt("%s f(void *context);",
			unk.empty() ? "void *" : (unk + " *").c_str());

		if (apply_definite_proto(r.create, d.c_str()))
			rv_log("    %a: prototype applied", r.create);
	}

	qstring c = "REverb: VST3 createInstance";

	c.cat_sprnt("\nclass \"%s\"", r.name.c_str());
	c.cat_sprnt("\ncid %s", r.cid.empty() ? "?" : r.cid.c_str());
	c.cat_sprnt("\ncategory %s", r.category.c_str());

	if (r.have_flags)
		c.cat_sprnt("\nclassFlags 0x%llX", (unsigned long long)r.flags);
	else
		c.append("\nclassFlags ?");

	add_func_cmt(r.create, c);

	qstring site;
	site.sprnt("registerClass: %s (%s)", r.name.c_str(), r.category.c_str());
	add_cmt(r.call_ea, site);

	if (r.callee != BADADDR && !in_extern_seg(r.callee) &&
		!has_user_name(get_flags(r.callee)) &&
		set_name(r.callee, "vst3_registerClass", SN_NOWARN | SN_FORCE)) {
		rv_log("    %a: named vst3_registerClass", r.callee);
	}

	mark_cfunc_dirty(r.create);
}

/**
 * @brief REverb's own default prototype for a create function,
 * `FUnknown *f(void *context)`, as apply_class() wrote it
 *
 * @return Whether the function at @p ea still has exactly that prototype
 */
bool default_create_proto(ea_t ea) {
	const qstring unk = resolve_type(VST3_SCOPE, "FUnknown");
	if (unk.empty()) return false;

	tinfo_t t;
	func_type_data_t fi;
	if (!get_tinfo(&t, ea) || !t.get_func_details(&fi) || fi.size() != 1)
		return false;

	qstring n;
	return fi.rettype.is_ptr() &&
		   fi.rettype.get_pointed_object().get_type_name(&n) && n == unk;
}

/**
 * @brief What a host does with a `createInstance` result: queries it from
 * `FUnknown`, or uses it as the component or controller it asked for
 */
constexpr const char* HOST_IFACES[] = { "IPluginBase", "IComponent",
	"IAudioProcessor", "IEditController", "IEditController2" };

/**
 * @brief Whether @p name (short or qualified) is one of #HOST_IFACES
 */
bool host_iface(const qstring& name) {
	const size_t sep = name.rfind(':');
	const char* base = name.c_str() + (sep == qstring::npos ? 0 : sep + 1);
	for (const char* h : HOST_IFACES)
		if (streq(base, h)) return true;
	return false;
}

/**
 * @brief Types the return of the create function of @p r as the interface
 * pinned at the returned offset
 *
 * `createInstance` returns the object cast to an interface, a subobject:
 * `return (IAudioProcessor *)obj;` is `obj + 0xC0` with multiple inheritance.
 * With a nonzero offset the object local is typed `char *` too, so the
 * adjustment reads in bytes instead of scaled by the interface size.
 *
 * @param r      The class
 * @param ifaces Subobject offset -> interface pinned there
 */
void type_return(
	const class_rec_t& r, const std::map<sval_t, qstring>& ifaces) {
	cfuncptr_t cf = decompile_at(r.create, DECOMP_NO_WAIT | DECOMP_NO_CACHE);
	loc_t l;
	if (cf == nullptr || !return_loc(cf, l)) {
		rv_log("  object of \"%s\": returned pointer not followed",
			r.name.c_str());
		return;
	}

	auto it = ifaces.find(sval_t(l.off));
	if (it == ifaces.end()) {
		rv_log(
			"  object of \"%s\": returns this+0x%llX, no interface typed there",
			r.name.c_str(), (uint64)l.off);
		return;
	}

	if (!host_iface(it->second)) {
		rv_log("  object of \"%s\": returns this+0x%llX, %s: kept FUnknown",
			r.name.c_str(), (uint64)l.off, it->second.c_str());
		return;
	}

	// A short name (RTTI base, queryInterface IID) or, from a vtable typed
	// before, a qualified one: both resolve in the scope
	const qstring iface = resolve_type(VST3_SCOPE, it->second.c_str());
	if (iface.empty()) {
		rv_log("  object of \"%s\": %s not declared", r.name.c_str(),
			it->second.c_str());
		return;
	}

	if (!default_create_proto(r.create)) return; // user's, or done already

	qstring d;
	d.sprnt("%s *f(void *context);", iface.c_str());
	if (!apply_definite_proto(r.create, d.c_str())) return;

	rv_log("  object of \"%s\": returns this+0x%llX -> %s *", r.name.c_str(),
		(uint64)l.off, iface.c_str());

	// Decompiled again with the new return type: lvar indices may move
	qstring label;
	label.sprnt("object of \"%s\"", r.name.c_str());
	type_return_base(r.create, label.c_str());
}

/**
 * @brief Whether the `<iface>_vtbl` type of @p iface fits in the vtable at @p
 * vt
 *
 * @param size Receives the size of the type in bytes, may be null
 */
bool vtbl_fits(const qstring& iface, ea_t vt, size_t* size = nullptr) {
	const qstring vtbl = resolve_type(VST3_SCOPE, (iface + "_vtbl").c_str());
	tinfo_t t;

	if (vtbl.empty() || !t.get_named_type(nullptr, vtbl.c_str()) ||
		!t.is_struct())
		return false;

	const size_t sz = t.get_size();
	if (sz == BADSIZE || sz == 0 || sz / ptr_size() > vtbl::extent(vt))
		return false;

	if (size != nullptr) *size = sz;

	return true;
}

/**
 * @brief The largest of the vtable types of @p names that fits the vtable at @p
 * vt, or empty
 */
qstring largest_fit(const std::vector<qstring>& names, ea_t vt) {
	qstring best;
	size_t best_size = 0;

	for (const qstring& n : names) {
		size_t sz;
		if (vtbl_fits(n, vt, &sz) && sz > best_size) {
			best = n;
			best_size = sz;
		}
	}

	return best;
}

/**
 * @brief The interface to pick for an RTTI vtable: the one with the largest
 * fitting base
 *
 * @param vt  Vtable address point
 * @param ci  RTTI of the vtable
 * @param why Explains an empty result
 */
qstring rtti_iface(ea_t vt, const rtti::class_info& ci, qstring& why) {
	std::vector<qstring> names;
	const qvector<qstring> bases = rtti::bases_at(ci, ci.sub_offset);

	for (const qstring& b : bases)
		names.push_back(rtti::short_name(b));

	const qstring best = largest_fit(names, vt);
	if (!best.empty() && best == rtti::short_name(ci.name)) {
		why = "vtable of the interface itself";
		return qstring();
	}

	if (best.empty()) {
		why = "no declared interface among RTTI bases (";
		for (size_t i = 0; i < bases.size(); i++)
			why.cat_sprnt("%s%s", i ? ", " : "", bases[i].c_str());
		why.append(")");
	}

	return best;
}

/**
 * @brief Collects the constants, objects and IID use of an `if` condition
 */
struct cond_consts_t : public ctree_visitor_t {
	const std::set<int>& iid_vars;
	std::set<uint64> q;
	std::set<uint32> d;
	std::vector<ea_t> objs;

	bool uses_iid = false;

	cond_consts_t(const std::set<int>& v)
		: ctree_visitor_t(CV_FAST), iid_vars(v) {}

	int idaapi visit_expr(cexpr_t* e) override {

		if (e->op == cot_var && iid_vars.count(e->v.idx) != 0) uses_iid = true;

		else if (e->op == cot_obj)
			objs.push_back(e->obj_ea);

		else if (e->op == cot_num) {
			const size_t sz = e->type.get_size();

			if (sz == 8) q.insert(e->numval());

			else if (sz != BADSIZE && sz <= 4)
				d.insert(uint32(e->numval()));
		}

		return 0;
	}
};

/**
 * @brief Finds the store `*obj = this + k`, giving the offset @c k of the
 * subobject
 */
struct obj_store_finder_t : public ctree_visitor_t {
	const std::set<int>& self_vars;
	const std::set<int>& obj_vars;
	bool found = false;
	sval_t k = 0;

	obj_store_finder_t(const std::set<int>& s, const std::set<int>& o)
		: ctree_visitor_t(CV_FAST), self_vars(s), obj_vars(o) {}

	int idaapi visit_expr(cexpr_t* e) override {
		if (e->op != cot_asg) return 0;

		loc_t dst, src;
		int sz;
		if (!fold_lvalue(e->x, nullptr, dst, sz) ||
			obj_vars.count(dst.var) == 0 || dst.off != 0 ||
			!fold_addr(e->y, nullptr, src) || self_vars.count(src.var) == 0)
			return 0;

		k = sval_t(src.off);
		found = true;
		return 1;
	}
};

/**
 * @brief Reads `queryInterface`: which IID comparisons lead to storing `this +
 * k` into the out parameter
 */
struct qi_reader_t : public ctree_visitor_t {
	std::set<int> self_vars, iid_vars, obj_vars;
	std::vector<std::pair<const iid_t*, std::array<uchar, 16>>> iids;
	bool numeric = true; /**< Little-endian: match qword/dword constants */
	std::map<qstring, sval_t> found;

	qi_reader_t() : ctree_visitor_t(CV_FAST) {}

	static uint64 le64(const uchar* p) {
		uint64 v = 0;
		for (int i = 7; i >= 0; i--)
			v = (v << 8) | p[i];
		return v;
	}

	bool matches(const cond_consts_t& cc, const uchar (&t)[16]) const {
		if (numeric) {
			if (cc.q.count(le64(t)) != 0 && cc.q.count(le64(t + 8)) != 0)
				return true;
			bool all = true;
			for (int i = 0; i < 4 && all; i++)
				all = cc.d.count(uint32(le64(t + 4 * i))) != 0;
			if (all) return true;
		}
		for (ea_t o : cc.objs) {
			uchar b[16];
			if (get_bytes(b, 16, o) == 16 && memcmp(b, t, 16) == 0) return true;
		}
		return false;
	}

	int idaapi visit_insn(cinsn_t* i) override {
		if (i->op != cit_if || i->cif == nullptr) return 0;

		cond_consts_t cc(iid_vars);
		cc.apply_to(&i->cif->expr, nullptr);
		if (!cc.uses_iid) return 0;

		std::vector<const iid_t*> hit;
		for (const auto& [iid, raw] : iids) {
			uchar t[16];
			memcpy(t, raw.data(), 16);
			if (matches(cc, t)) hit.push_back(iid);
		}
		if (hit.empty()) return 0;

		obj_store_finder_t sf(self_vars, obj_vars);
		sf.apply_to(i->cif->ithen, nullptr);
		if (!sf.found) return 0;

		for (const iid_t* iid : hit)
			found.emplace(iid->name, sf.k);
		return 0;
	}
};

/**
 * @brief Follows copies of the `this`, IID and out-parameter locals
 */
struct copy_finder_t : public ctree_visitor_t {
	std::set<int>* sets[3];
	copy_finder_t(std::set<int>* a, std::set<int>* b, std::set<int>* c)
		: ctree_visitor_t(CV_FAST), sets{ a, b, c } {}

	int idaapi visit_expr(cexpr_t* e) override {
		for (std::set<int>* s : sets)
			absorb_copy(e, *s);
		return 0;
	}
};

/**
 * @brief IID name -> `this` offset, from `queryInterface` in slot 0 of @p vt0
 */
std::map<qstring, sval_t> read_query_interface(ea_t vt0) {
	const ea_t qi = vtbl::slot_func(vt0, 0);
	if (qi == BADADDR) {
		rv_log("    vtable %a: no queryInterface in slot 0", vt0);
		return {};
	}

	cfuncptr_t cf = decompile_at(qi, DECOMP_NO_WAIT | DECOMP_NO_CACHE);
	if (cf == nullptr) return {};
	if (cf->argidx.size() < 3) {
		rv_log("    queryInterface %a: %u arguments, expected 3", qi,
			unsigned(cf->argidx.size()));
		return {};
	}

	qi_reader_t r;
	r.self_vars.insert(cf->argidx[0]);
	r.iid_vars.insert(cf->argidx[1]);
	r.obj_vars.insert(cf->argidx[2]);
	r.numeric = !inf_is_be();

	copy_finder_t cp(&r.self_vars, &r.iid_vars, &r.obj_vars);
	cp.apply_to(&cf->body, nullptr);

	const bool com = inf_get_filetype() == f_PE;
	for (const iid_t& k : KNOWN_IIDS) {
		uchar t[16];
		tuid_from_longs(k.l, com, t);
		std::array<uchar, 16> a;
		memcpy(a.data(), t, 16);
		r.iids.emplace_back(&k, a);
	}

	r.apply_to(&cf->body, nullptr);

	if (r.found.empty())
		rv_log("    queryInterface %a: no IID comparisons read", qi);
	else
		for (const auto& [n, k] : r.found)
			rv_log("    queryInterface %a: %s -> this%+lld", qi, n.c_str(),
				(long long)k);
	return r.found;
}

/** @brief The interface of an `X_vtbl` typed vtable, or empty */
qstring vtbl_iface(ea_t vt) {
	tinfo_t cur;
	qstring n;
	if (!get_tinfo(&cur, vt) || !cur.get_type_name(&n) || !n.ends_with("_vtbl"))
		return qstring();
	n.resize(n.length() - 5);
	return n;
}

/** @brief Whether the vtable at @p vt has a user type or a `_vtbl` type already
 */
bool already_typed(ea_t vt) { return is_userti(vt) || !vtbl_iface(vt).empty(); }

/**
 * @brief Finds the vtables stored by the create function of @p r and pins their
 * interface types
 *
 * The interface comes from RTTI, or without it from the IIDs `queryInterface`
 * compares. Stores are read from the function itself or from the constructor it
 * calls.
 */
void type_object(const class_rec_t& r) {

	// Subobject offset -> vtable, from an inlined or a leaf constructor
	std::map<sval_t, ea_t> vts;
	cfuncptr_t cf = decompile_at(r.create, DECOMP_NO_WAIT | DECOMP_NO_CACHE);
	if (!cf) return;

	vtbl::store_finder_t vf;
	vf.apply_to(&cf->body, nullptr);

	if (!vf.hits.empty()) {
		// The object is the vtable store target that is returned
		int var = vf.hits.front().var_idx;
		loc_t ret;
		if (return_loc(cf, ret))
			for (const auto& h : vf.hits)
				if (h.var_idx == ret.var) {
					var = h.var_idx;
					break;
				}
		vts = vtbl::ctor_vtables(cf, var);
	}

	ctor_candidate_finder_t cfinder;
	cfinder.apply_to(&cf->body, nullptr);

	for (ea_t cand : cfinder.candidates) {
		if (ensure_func(cand) == nullptr) continue;

		cfuncptr_t ccf = decompile_at(cand, DECOMP_NO_WAIT | DECOMP_NO_CACHE);
		if (ccf == nullptr || ccf->argidx.empty()) continue;

		vts = vtbl::ctor_vtables(ccf, ccf->argidx[0]);
		if (!vts.empty()) {
			rv_log("    constructor %a", cand);
			break;
		}
	}

	if (vts.empty()) {
		rv_log("  object of \"%s\": no vtable stores found", r.name.c_str());
		return;
	}

	bool qi_read = false;
	std::map<qstring, sval_t> qi;
	std::map<sval_t, qstring> ifaces; // this offset -> typed interface

	for (const auto& [off, vt] : vts) {
		qstring iface, why;
		rtti::class_info ci;

		if (rtti::parse_rtti(vt, &ci)) {
			iface = rtti_iface(vt, ci, why);

		} else {
			if (!qi_read) {
				auto it0 = vts.find(0);
				if (it0 != vts.end()) qi = read_query_interface(it0->second);
				qi_read = true;
			}

			std::vector<qstring> names;
			for (const auto& [n, k] : qi)
				if (k == off) names.push_back(n);

			iface = largest_fit(names, vt);
			if (iface.empty())
				why = names.empty()
						  ? "no RTTI, queryInterface returns no IID here"
						  : "no RTTI, no declared vtable type fits the IIDs";
		}

		if (iface.empty()) {
			rv_log("  object of \"%s\": vtable %a at +0x%llX left alone: %s",
				r.name.c_str(), vt, (uint64)off, why.c_str());
			continue;
		}
		if (already_typed(vt)) {
			rv_log("  object of \"%s\": vtable %a at +0x%llX already typed",
				r.name.c_str(), vt, (uint64)off);
			const qstring cur = vtbl_iface(vt);
			if (!cur.empty()) ifaces[off] = cur;
			continue;
		}

		vtbl::pin(vt, (iface + "_vtbl").c_str());
		ifaces[off] = iface;
		rv_log("  object of \"%s\": vtable %a at +0x%llX -> %s", r.name.c_str(),
			vt, (uint64)off, iface.c_str());
	}

	type_return(r, ifaces);
}

/**
 * @brief Adds the class @p r to the report
 *
 * @param missing Shown as the `createInstance` row when no create function was
 * found, null otherwise
 */
void report_class(const class_rec_t& r, const char* missing) {

	qstring title;
	title.sprnt("VST3 class \"%s\"", r.name.empty() ? "?" : r.name.c_str());
	report::heading(title, r.create);

	report::row("category", r.category.empty() ? "(none)" : r.category);

	qstring v;
	for (const char* f :
		{ "subCategories", "vendor", "version", "sdkVersion" }) {
		if (str_field(*r.s, f, v) && !v.empty()) report::row(f, v);
	}

	report::row("cid", r.cid.empty() ? "(unknown)" : r.cid);

	if (r.have_flags) {
		v.sprnt("0x%llX", (unsigned long long)r.flags);
		report::row("classFlags", v);
	}

	if (missing != nullptr) report::row("createInstance", missing);
	else
		report::addr_row("createInstance", r.create);
}

} // namespace

void label_classes(cfunc_t* cf, const std::vector<factory_struct_t>& structs) {
	if (!cf) return;

	call_collector_t cc;
	cc.apply_to(&cf->body, nullptr);

	std::vector<class_rec_t> recs, unmatched;
	for (const factory_struct_t& s : structs) {
		if (!s.type || strcmp(s.type, "PClassInfo2") != 0) continue;

		class_rec_t r;
		r.s = &s;
		str_field(s, "name", r.name);
		str_field(s, "category", r.category);
		r.cid = cid_hex(s);

		r.have_flags = num_field(s, "classFlags", r.flags);

		if (s.root != GLOBAL_ROOT && s.base_off != 0) {
			rv_log("class \"%s\" [%s]: local PClassInfo2 at var%d%+lld, "
				   "skipped",
				r.name.c_str(), r.category.c_str(), s.root,
				(long long)s.base_off);
			unmatched.push_back(std::move(r));
			continue;
		}

		if (!find_register_call(cc.calls, s, r)) {
			rv_log("class \"%s\" [%s]: no registerClass call found",
				r.name.c_str(), r.category.c_str());
			unmatched.push_back(std::move(r));
			continue;
		}

		rv_log("class \"%s\" [%s] -> createFunc %a", r.name.c_str(),
			r.category.c_str(), r.create);
		recs.push_back(std::move(r));
	}

	if (recs.empty() && unmatched.empty())
		report::row("classes", "(no PClassInfo2 found)");

	for (const class_rec_t& r : unmatched)
		report_class(r, "(no registerClass call found)");

	if (recs.empty()) return;

	std::map<qstring, int> uses;
	for (const class_rec_t& r : recs)
		uses[sanitize(r.name)]++;

	std::set<ea_t> done;
	for (const class_rec_t& r : recs) {
		qstring n = sanitize(r.name);

		if (n.empty()) n = "class";

		if (uses[sanitize(r.name)] > 1) {

			if (r.category == "Audio Module Class") {
				n.append("_processor");
			} else if (r.category == "Component Controller Class") {
				n.append("_controller");
			} else {
				n.cat_sprnt("_%s", sanitize(r.category).c_str());
			}
		}

		if (!done.insert(r.create).second) {
			rv_log("  createFunc %a shared with another class", r.create);
			qstring site;

			site.sprnt(
				"registerClass: %s (%s)", r.name.c_str(), r.category.c_str());
			add_cmt(r.call_ea, site);
			continue;
		}

		apply_class(r, qstring("vst3_createInstance_") + n);
		type_object(r);
	}

	for (const class_rec_t& r : recs)
		report_class(r, nullptr);

	mark_cfunc_dirty(cf->entry_ea);
}

} // namespace rv::vst3
