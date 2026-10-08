#include "VST2/opcode.hpp"
#include "common/analysis.hpp"
#include "util.hpp"
#include <algorithm>
#include <bytes.hpp>
#include <hexrays.hpp>
#include <llong.hpp>
#include <nalt.hpp>
#include <pro.h>
#include <set>
#include <typeinf.hpp>
#include <utility>
#include <vector>
namespace rv::vst2 {

namespace {

bool read_cstr(ea_t ea, qstring& out) {

	if (!is_strlit(get_flags(ea))) {
		size_t n = get_max_strlit_length(ea, STRTYPE_C, ALOPT_IGNHEADS);
		if (n < 2 || n > 256) return false;
	}

	if (get_strlit_contents(&out, ea, -1, STRTYPE_C) <= 0) return false;

	for (char c : out) {
		if (uchar(c) < 0x20 || uchar(c) >= 0x7F) return false;
	}

	return !out.empty();
}

bool is_opcode_expr(cexpr_t* e, const std::set<int>& vars, int64& bias) {
	e = strip_casts(e);
	bias = 0;

	if (e->op == cot_add || e->op == cot_sub) {
		cexpr_t* n = strip_casts(e->y);
		if (n->op != cot_num) return false;

		const int64 k = int64(n->numval());
		bias = (e->op == cot_sub) ? k : -k;
		e = strip_casts(e->x);
	}

	return e->op == cot_var && vars.count(e->v.idx);
}

struct literal_collector_t : public ctree_visitor_t {
	std::vector<qstring> strs;
	std::vector<uint64> rets;

	literal_collector_t() : ctree_visitor_t(CV_FAST) {}

	int idaapi visit_expr(cexpr_t* e) override {
		qstring s;

		if (e->op == cot_str && e->string) s = e->string;
		else if (e->op != cot_obj || is_code_addr(e->obj_ea) ||
				 !read_cstr(e->obj_ea, s))
			return 0;

		if (std::find(strs.begin(), strs.end(), s) == strs.end())
			strs.push_back(s);

		return 0;
	}

	int idaapi visit_insn(cinsn_t* i) override {
		if (i->op != cit_return || !i->creturn) return 0;

		cexpr_t* r = strip_casts(&i->creturn->expr);
		if (r->op == cot_num) rets.push_back(r->numval());
		return 0;
	}
};

struct dispatch_walker_t : public ctree_visitor_t {
	std::set<int> opvars;
	std::map<uint32, cinsn_t*> bodies;
	std::vector<std::pair<ea_t, int>> fwds;

	dispatch_walker_t() : ctree_visitor_t(CV_FAST) {}

	void add_body(uint64 v, int64 bias, cinsn_t* body) {
		const uint64 op = v + uint64(bias);
		if (op <= MAX_OPCODE && body) bodies.emplace(uint32(op), body);
	}

	int idaapi visit_insn(cinsn_t* i) override {
		int64 bias;

		if (i->op == cit_switch) {
			if (!is_opcode_expr(&i->cswitch->expr, opvars, bias)) return 0;

			for (ccase_t& c : i->cswitch->cases)
				for (uint64 v : c.values)
					add_body(v, bias, &c);

			return 0;
		}

		if (i->op == cit_if) {

			cexpr_t* c = strip_casts(&i->cif->expr);
			if (c->op != cot_eq && c->op != cot_ne) return 0;

			cexpr_t* a = strip_casts(c->x);
			cexpr_t* b = strip_casts(c->y);

			if (a->op == cot_num) std::swap(a, b);
			if (b->op != cot_num || !is_opcode_expr(a, opvars, bias)) return 0;

			add_body(b->numval(), bias,
				c->op == cot_eq ? i->cif->ithen : i->cif->ielse);
			return 0;
		}

		return 0;
	}

	int idaapi visit_expr(cexpr_t* e) override {

		if (e->op == cot_asg) {
			cexpr_t* l = strip_casts(e->x);
			cexpr_t* r = strip_casts(e->y);

			if (l->op == cot_var && r->op == cot_var && opvars.count(r->v.idx))
				opvars.insert(l->v.idx);

			return 0;
		}

		if (e->op == cot_call && e->a != nullptr) {
			cexpr_t* f = strip_casts(e->x);
			if (f->op != cot_obj) return 0;

			for (int k = 0; k < e->a->size(); k++) {
				cexpr_t* a = strip_casts(&e->a->at(k));

				if (a->op == cot_var && opvars.count(a->v.idx)) {
					fwds.push_back({ f->obj_ea, int(k) });
					break;
				}
			}
		}

		return 0;
	}
};

void harvest(uint32 op, cinsn_t* body, dispatcher_info_t& out) {
	if (!out.handled.insert(op).second) return;

	literal_collector_t lc;
	lc.apply_to(body, nullptr);

	for (const str_op_t& s : STRING_OPS) {
		if (s.op != op) continue;

		for (const qstring& str : lc.strs) {
			if (str.length() < s.max_len) {
				out.strings[op] = str;
				break;
			}
		}
	}

	if (std::find(std::begin(CONST_OPS), std::end(CONST_OPS), op) !=
			std::end(CONST_OPS) &&
		!lc.rets.empty() &&
		std::all_of(lc.rets.begin(), lc.rets.end(),
			[&](uint64 r) { return r == lc.rets[0]; }))

		out.consts[op] = lc.rets[0];

	if (op == 51) // effCanDo
		for (const qstring& s : lc.strs)
			if (std::find(out.can_do.begin(), out.can_do.end(), s) ==
				out.can_do.end())
				out.can_do.push_back(s);
}

void walk(ea_t func_ea, int op_argpos, int depth, std::set<ea_t>& seen,
	dispatcher_info_t& out) {
	if (depth > MAX_DEPTH || !seen.insert(func_ea).second) return;

	func_t* f = ensure_func(func_ea);
	if (f == nullptr) return;

	hexrays_failure_t hf;
	cfuncptr_t cf = decompile(f, &hf, DECOMP_NO_WAIT);
	if (cf == nullptr) {
		rv_log("Dispatcher: failed to decompile %a: %s", func_ea,
			hf.desc().c_str());
		return;
	}
	if (op_argpos >= int(cf->argidx.size())) return;

	dispatch_walker_t w;
	w.opvars.insert(cf->argidx[op_argpos]);
	w.apply_to(&cf->body, nullptr);

	rv_log("Dispatcher: %a (depth %d): %u opcode cases, %u forwarding calls",
		func_ea, depth, unsigned(w.bodies.size()), unsigned(w.fwds.size()));

	if (!w.bodies.empty() && out.switch_func == BADADDR)
		out.switch_func = func_ea;

	// Harvest while cf is alive
	for (auto& [op, body] : w.bodies)
		harvest(op, body, out);

	for (auto& [callee, pos] : w.fwds)
		walk(callee, pos, depth + 1, seen, out);
}

} // namespace

qstring opcode_name(uint32 op) {
	tinfo_t t;
	edm_t edm;

	if (t.get_named_type(get_idati(), "VstOpcode") &&
		t.get_edm_by_value(&edm, op) >= 0)
		return edm.name;

	qstring s;
	s.sprnt("opcode_%u", op);
	return s;
}

bool analyze_dispatcher(ea_t dispatcher_ea, dispatcher_info_t& out) {
	out = {};
	out.dispatcher = dispatcher_ea;

	std::set<ea_t> seen;
	walk(dispatcher_ea, 1, 0, seen, out);

	return !out.handled.empty();
}

void apply_dispatcher(const dispatcher_info_t& info) {
	qstring c = "REverb: VST2 dispatcher";
	if (info.switch_func != info.dispatcher)
		c.cat_sprnt(" (opcode switch in %a)", info.switch_func);

	for (const auto& [op, s] : info.strings)
		c.cat_sprnt("\n%s: \"%s\"", opcode_name(op).c_str(), s.c_str());

	for (const auto& [op, v] : info.consts) {
		if (op == 35) // effGetPlugCategory
			c.cat_sprnt("\ncategory: %s (%llu)", category_name(v),
				(unsigned long long)v);
		else if (op == 58) // effGetVsTVersion
			c.cat_sprnt("\nVST Interface version: %llu", (unsigned long long)v);
		else
			c.cat_sprnt(
				"\n%s: %llu", opcode_name(op).c_str(), (unsigned long long)v);
	}

	if (!info.can_do.empty()) {
		c += "\ncanDo:";
		for (const qstring& s : info.can_do)
			c.cat_sprnt(" %s", s.c_str());
	}

	c.cat_sprnt("\nhandles %u opcodes:", unsigned(info.handled.size()));
	for (uint32 op : info.handled)
		c.cat_sprnt(" %s", opcode_name(op).c_str());

	if (func_t* f = get_func(info.dispatcher))
		set_func_cmt(f, c.c_str(), false);
	mark_cfunc_dirty(info.dispatcher);

	// Name the real switch function when the dispatcher is just a wrapper
	if (info.switch_func != BADADDR && info.switch_func != info.dispatcher &&
		!has_user_name(get_flags(info.switch_func))) {
		set_name(
			info.switch_func, "vst2_dispatcher_impl", SN_NOWARN | SN_FORCE);
		mark_cfunc_dirty(info.switch_func);
	}

	rv_log("  %s", c.c_str());
}

} // namespace rv::vst2