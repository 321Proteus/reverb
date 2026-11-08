#include "factory.hpp"
#include "core/log.hpp"
#include "cxx/vtable.hpp"
#include "db/read.hpp"
#include "db/segments.hpp"
#include "decomp/analysis.hpp"
#include "decomp/call.hpp"
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

struct iid_t {
	const char* name;
	uint32 l[4];
};

constexpr iid_t KNOWN_IIDS[] = {
#define RV_IID(n, l1, l2, l3, l4) { #n, { l1, l2, l3, l4 } },
#include "iids.inc"
#undef RV_IID
};

// A PClassInfo2 and the registerClass call that passes it
struct class_rec_t {
	const factory_struct_t* s = nullptr;
	qstring name, category, cid;
	bool have_flags = false;
	uint64 flags = 0;

	ea_t call_ea = BADADDR;
	ea_t callee = BADADDR;
	ea_t create = BADADDR;
};

bool str_field(const factory_struct_t& s, const char* f, qstring& out) {
	auto it = s.fields.find(f);

	return it != s.fields.end() && it->second.v.t == val_t::ADDR &&
		   read_utf8(it->second.v.ea, out);
}

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

// The call that passes s and a code address is the create function
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

// Appends text unless the function comment already holds it
void add_func_cmt(ea_t ea, const qstring& text) {
	qstring cur;
	get_func_cmt_ea(&cur, ea, false);
	if (cur.find(text) != qstring::npos) return;

	if (!cur.empty()) cur.append('\n');
	cur.append(text);
	set_func_cmt_ea(ea, cur.c_str(), false);
}

void add_cmt(ea_t ea, const qstring& text) {
	qstring cur;
	get_cmt(&cur, ea, false);
	if (cur.find(text) == qstring::npos) append_cmt(ea, text.c_str(), false);
}

void name_func(ea_t ea, const qstring& nm) {
	if (has_user_name(get_flags(ea))) {
		qstring cur;
		get_ea_name(&cur, ea);
		rv_log("    %a: keeping existing name '%s'", ea, cur.c_str());
	} else if (set_name(ea, nm.c_str(), SN_NOWARN | SN_FORCE)) {
		rv_log("    %a: named %s", ea, nm.c_str());
	}
}

void apply_class(const class_rec_t& r, const qstring& fname) {
	name_func(r.create, fname);

	if (!is_userti(r.create)) {
		qstring d;
		tinfo_t t;

		d.sprnt("%s f(void *context);",
			t.get_named_type(nullptr, "FUnknown") ? "FUnknown *" : "void *");

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

// Locals returned by the function, through this with offset
struct return_var_finder_t : public ctree_visitor_t {
	std::set<int> vars;

	return_var_finder_t() : ctree_visitor_t(CV_FAST) {}

	int idaapi visit_insn(cinsn_t* i) override {
		loc_t l;

		if (i->op == cit_return && i->creturn != nullptr &&
			fold_addr(&i->creturn->expr, nullptr, l)) {
			vars.insert(l.var);
		}

		return 0;
	}
};

bool vtbl_fits(const qstring& iface, ea_t vt, size_t* size = nullptr) {
	tinfo_t t;
	if (!t.get_named_type(nullptr, (iface + "_vtbl").c_str()) || !t.is_struct())
		return false;

	const size_t sz = t.get_size();
	if (sz == BADSIZE || sz == 0 || sz / ptr_size() > vtbl::extent(vt))
		return false;

	if (size != nullptr) *size = sz;

	return true;
}

// Largest fitting vtable type among names
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

/*
	The interface to pick for an RTTI vtable is the one with largest fitting
	base. Only return when it's in OBJECT_IFACES, so a pin makes
   add_rtti_vtables stay the same. why explains an empty result
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

// Constants of an if condition
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

// *obj = this + k
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

struct qi_reader_t : public ctree_visitor_t {
	std::set<int> self_vars, iid_vars, obj_vars;
	std::vector<std::pair<const iid_t*, std::array<uchar, 16>>> iids;
	bool numeric = true; // little-endian: match qword/dword constants
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

// IID name -> this offset, from queryInterface in slot 0 of vt0
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

bool already_typed(ea_t vt) {
	if (is_userti(vt)) return true;
	tinfo_t cur;
	qstring n;
	return get_tinfo(&cur, vt) && cur.get_type_name(&n) && n.ends_with("_vtbl");
}

void type_object(const class_rec_t& r) {

	// Subobject offset -> vtable, from an inlined or a leaf constructor
	std::map<sval_t, ea_t> vts;
	cfuncptr_t cf = decompile_at(r.create, DECOMP_NO_WAIT | DECOMP_NO_CACHE);
	if (!cf) return;

	vtbl::store_finder_t vf;
	vf.apply_to(&cf->body, nullptr);

	if (!vf.hits.empty()) {
		return_var_finder_t rf;
		rf.apply_to(&cf->body, nullptr);

		int var = vf.hits.front().var_idx;
		for (const auto& h : vf.hits)
			if (rf.vars.count(h.var_idx) != 0) {
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
			continue;
		}

		vtbl::pin(vt, (iface + "_vtbl").c_str());
		rv_log("  object of \"%s\": vtable %a at +0x%llX -> %s", r.name.c_str(),
			vt, (uint64)off, iface.c_str());
	}
}

} // namespace

void label_classes(cfunc_t* cf, const std::vector<factory_struct_t>& structs) {
	if (!cf) return;

	call_collector_t cc;
	cc.apply_to(&cf->body, nullptr);

	std::vector<class_rec_t> recs;
	for (const factory_struct_t& s : structs) {
		if (!s.type || strcmp(s.type, "PClassInfo2") != 0) continue;

		class_rec_t r;
		r.s = &s;
		str_field(s, "name", r.name);
		str_field(s, "category", r.category);
		r.cid = cid_hex(s);

		if (auto f = s.fields.find("classFlags");
			f != s.fields.end() && f->second.v.t == val_t::NUM) {
			r.have_flags = true;
			r.flags = f->second.v.num;
		}

		if (auto ob = s.or_bits.find("classFlags"); ob != s.or_bits.end()) {
			r.have_flags = true;
			r.flags |= ob->second;
		}

		if (s.root != GLOBAL_ROOT && s.base_off != 0) {
			rv_log("class \"%s\" [%s]: local PClassInfo2 at var%d%+lld, "
				   "skipped",
				r.name.c_str(), r.category.c_str(), s.root,
				(long long)s.base_off);
			continue;
		}

		if (!find_register_call(cc.calls, s, r)) {
			rv_log("class \"%s\" [%s]: no registerClass call found",
				r.name.c_str(), r.category.c_str());
			continue;
		}

		rv_log("class \"%s\" [%s] -> createFunc %a", r.name.c_str(),
			r.category.c_str(), r.create);
		recs.push_back(std::move(r));
	}

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

	mark_cfunc_dirty(cf->entry_ea);
}

} // namespace rv::vst3
