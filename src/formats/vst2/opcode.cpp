#include "opcode.hpp"
#include "ui/report.hpp"
#include "core/log.hpp"
#include "db/segments.hpp"
#include "decomp/ctree.hpp"
#include "decomp/analysis.hpp"
#include "patterns/dispatch.hpp"
#include "decomp/stores.hpp"
#include <algorithm>
#include <bytes.hpp>
#include <funcs.hpp>
#include <hexrays.hpp>
#include <name.hpp>
#include <pro.h>
#include <set>
#include <typeinf.hpp>
#include <vector>

namespace rv::vst2 {

namespace {

// Buffer size of a string opcode, 0 for others
size_t max_str_len(uint32 op) {
	for (const str_op_t& s : STRING_OPS)
		if (s.op == op) return s.max_len;
	return 0;
}

bool is_const_op(uint32 op) {
	return std::find(std::begin(CONST_OPS), std::end(CONST_OPS), op) !=
		   std::end(CONST_OPS);
}

bool single_value(const std::vector<uint64>& v) {
	return !v.empty() &&
		   std::all_of(v.begin(), v.end(), [&](uint64 r) { return r == v[0]; });
}

// Source paths from asserts and logging are never names
bool is_sourcepath(const qstring& s) {
	return s.find('/') == qstring::npos && s.find('\\') == qstring::npos;
}

struct harvest_t {
	bool want_str;
	bool want_const;
	std::set<ea_t> seen;
	std::vector<qstring> strs;
	std::vector<uint64> rets;

	bool need_more() const {
		return (want_str && strs.empty()) ||
			   (want_const && !single_value(rets));
	}

	void add_str(const qstring& s) {
		if (is_sourcepath(s) &&
			std::find(strs.begin(), strs.end(), s) == strs.end())
			strs.push_back(s);
	}

	// Literals in a case body, then in the handlers it calls on the object
	// or with the data pointer: SDK getEffectName(ptr) overrides, JUCE
	// handleGetPlugInName(args), an out-of-line vst_strncpy(ptr, "Name").
	void gather(citem_t* root, cfunc_t* cf, const obj_tracker_t& obj,
		const std::set<int>& ptrs, int depth) {
		literal_collector_t lc;
		lc.apply_to(root, nullptr);

		for (const qstring& s : lc.strs)
			add_str(s);

		if (want_const && rets.empty()) {
			if (!lc.rets.empty()) rets = lc.rets;
			else if (lc.calls.empty())
				rets = lc.asgs; // result = 2400; break;
		}

		// Inlined strcpy into the output buffer
		if (want_str && strs.empty() && !ptrs.empty()) {
			store_collector_t sc(cf);
			sc.apply_to(root, nullptr);
			for (int p : ptrs) {
				qstring s;
				if (inline_string(sc.stores, p, s)) add_str(s);
			}
		}

		if (depth >= MAX_HARVEST_DEPTH) return;

		for (cexpr_t* call : lc.calls) {
			if (!need_more()) return;

			cexpr_t* fn = strip_casts(call->x);
			ea_t t = BADADDR;
			if (fn->op == cot_obj && is_code_addr(fn->obj_ea))
				t = resolve_thunk(fn->obj_ea);
			else
				t = obj.vcall_target(fn);
			if (t == BADADDR) continue;

			const int ppos = arg_pos(call, ptrs);
			const bool on_obj = call->a != nullptr && !call->a->empty() &&
								obj.is_object(&call->a->at(0));
			if (!on_obj && (ppos < 0 || !want_str)) continue; // unrelated
			if (!seen.insert(t).second) continue;

			cfuncptr_t sub = decompile_at(t);
			if (sub == nullptr) continue;

			const int nargs = int(sub->argidx.size());
			obj_tracker_t sobj;
			std::set<int> sptrs;
			if (on_obj && nargs > 0) {
				sobj.objvars.insert(sub->argidx[0]);
				sobj.vtable = obj.vtable;
			}
			if (ppos >= 0 && ppos < nargs) sptrs.insert(sub->argidx[ppos]);

			gather(&sub->body, &*sub, sobj, sptrs, depth + 1);
		}
	}
};

void harvest(const dispatch_case_t& c, dispatcher_info_t& out) {
	const uint32 op = uint32(c.sel);
	if (!out.handled.insert(op).second) return; // outer level already has it

	const size_t max_len = max_str_len(op);
	harvest_t h{ max_len > 0 || op == EFF_CAN_DO, is_const_op(op) };
	if (!h.want_str && !h.want_const) return;

	h.gather(c.body, c.cf, c.obj, c.ptrvars, 0);

	if (max_len > 0) {
		for (const qstring& s : h.strs) {
			if (s.length() < max_len) {
				out.strings[op] = s;
				break;
			}
		}
	}

	if (h.want_const && single_value(h.rets)) out.consts[op] = h.rets[0];

	if (op == EFF_CAN_DO)
		for (const qstring& s : h.strs)
			if (std::find(out.can_do.begin(), out.can_do.end(), s) ==
				out.can_do.end())
				out.can_do.push_back(s);
}

// ------------------------------------------------------- vtable from entry

// Direct calls that pass the audioMaster var (or a copy of it)
struct am_call_finder_t : public ctree_visitor_t {
	std::set<int> am;
	struct call_t {
		ea_t callee;
		int pos;  // audioMaster position
		int arg0; // first argument's local, -1 if none
	};
	std::vector<call_t> calls;

	explicit am_call_finder_t(int v) : ctree_visitor_t(CV_FAST) {
		am.insert(v);
	}

	int idaapi visit_expr(cexpr_t* e) override {
		if (absorb_copy(e, am)) return 0;

		const int pos = arg_pos(e, am);
		if (pos < 0) return 0;
		cexpr_t* fn = strip_casts(e->x);
		if (fn->op != cot_obj) return 0; // skips audioMaster(...) itself

		cexpr_t* a0 = strip_casts(&e->a->at(0));
		calls.push_back(
			{ fn->obj_ea, pos, a0->op == cot_var ? a0->v.idx : -1 });
		return 0;
	}
};

// VSTPluginMain -> [createEffectInstance] -> Plugin::Plugin(this, audioMaster)
// The first callee taking audioMaster next to an object is the constructor.
ea_t find_vtable_from(ea_t fn_ea, int am_pos, int depth) {
	if (depth > MAX_ENTRY_DEPTH) return BADADDR;

	cfuncptr_t cf = decompile_at(fn_ea);
	if (cf == nullptr || am_pos >= int(cf->argidx.size())) return BADADDR;

	am_call_finder_t finder(cf->argidx[am_pos]);
	finder.apply_to(&cf->body, nullptr);

	for (const auto& c : finder.calls) {
		if (c.pos > 0) {
			// A derived ctor inlined around the call stores into the same
			// local after it; otherwise the callee is the most derived ctor
			ea_t vt = vtbl::ctor_vtable(cf, c.arg0);
			if (vt == BADADDR) {
				cfuncptr_t cc = decompile_at(c.callee);
				if (cc != nullptr && !cc->argidx.empty())
					vt = vtbl::ctor_vtable(cc, cc->argidx[0]);
			}
			if (vt != BADADDR) {
				rv_log("Plugin ctor %a, vtable %a", c.callee, vt);
				return vt;
			}
		}
		if (ea_t v = find_vtable_from(c.callee, c.pos, depth + 1); v != BADADDR)
			return v;
	}
	return BADADDR;
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

ea_t find_plugin_vtable(ea_t entry_ea) {
	if (entry_ea == BADADDR) return BADADDR;
	// audioMaster is entry arg 0
	return find_vtable_from(entry_ea, 0, 0);
}

bool analyze_dispatcher(
	ea_t dispatcher_ea, ea_t vtable, int64 object_off, dispatcher_info_t& out) {
	out = {};
	out.dispatcher = dispatcher_ea;
	out.vtable = vtable;
	if (vtable != BADADDR) out.class_name = rtti::class_name(vtable);

	/*
	dispatcher(effect, opcode, index, value, ptr, opt), object = effect->object
	*/
	dispatch_cfg_t cfg;
	cfg.root.sel = ARG_OPC;
	cfg.root.ptr = ARG_PTR;
	cfg.root.obj_src = 0;
	cfg.root.obj_off = object_off;
	cfg.root.vtable = vtable;
	cfg.max_sel = MAX_OPCODE;
	cfg.max_depth = MAX_DEPTH;

	const dispatch_result_t r = walk_dispatch(
		dispatcher_ea, cfg, [&](const dispatch_case_t& c) { harvest(c, out); });

	out.switch_func = r.switch_func;
	return !out.handled.empty();
}

static void prepare_report(const std::vector<dispatcher_info_t>& infos) {
	const dispatcher_info_t& first = infos.front();

	report::heading("VST2 dispatcher", first.dispatcher);
	report::addr_row("dispatcher", first.dispatcher);
	if (first.switch_func != BADADDR && first.switch_func != first.dispatcher)
		report::addr_row("opcode switch", first.switch_func);

	for (const dispatcher_info_t& info : infos) {
		if (info.vtable != BADADDR) {
			qstring o;
			o.sprnt("%s (vtable %a)",
				info.class_name.empty() ? "?" : info.class_name.c_str(),
				info.vtable);
			report::row("plugin object", o, info.vtable);
		}

		for (const auto& [op, s] : info.strings)
			report::row(opcode_name(op), qstring("\"") + s + "\"");

		for (const auto& [op, v] : info.consts) {
			qstring val;
			if (op == EFF_GET_PLUG_CATEGORY)
				val.sprnt("%s (%llu)", category_name(v), (unsigned long long)v);
			else
				val.sprnt("%llu", (unsigned long long)v);
			report::row(opcode_name(op), val);
		}

		for (const qstring& s : info.can_do)
			report::row("canDo", qstring("\"") + s + "\"");

		for (const uint32 op : info.handled) {
			qstring v;
			v.sprnt("%s (%u)", opcode_name(op).c_str(), op);
			report::row("handles", v);
		}
	}
}

void apply_dispatcher(const std::vector<dispatcher_info_t>& infos) {
	if (infos.empty()) return;
	const dispatcher_info_t& first = infos.front();

	qstring c = "REverb: VST2 dispatcher";
	if (first.switch_func != BADADDR && first.switch_func != first.dispatcher)
		c.cat_sprnt(" (opcode switch in %a)", first.switch_func);

	for (const dispatcher_info_t& info : infos) {
		if (info.vtable != BADADDR)
			c.cat_sprnt("\n%s%s object, vtable %a",
				infos.size() > 1 ? "--- " : "",
				info.class_name.empty() ? "plugin" : info.class_name.c_str(),
				info.vtable);

		for (const auto& [op, s] : info.strings)
			c.cat_sprnt("\n%s: \"%s\"", opcode_name(op).c_str(), s.c_str());

		for (const auto& [op, v] : info.consts) {
			if (op == EFF_GET_PLUG_CATEGORY)
				c.cat_sprnt("\ncategory: %s (%llu)", category_name(v),
					(unsigned long long)v);
			else if (op == EFF_GET_VST_VERSION)
				c.cat_sprnt(
					"\nVST interface version: %llu", (unsigned long long)v);
			else
				c.cat_sprnt("\n%s: %llu", opcode_name(op).c_str(),
					(unsigned long long)v);
		}

		if (!info.can_do.empty()) {
			c += "\ncanDo:";
			for (const qstring& s : info.can_do)
				c.cat_sprnt(" %s", s.c_str());
		}

		c.cat_sprnt("\nhandles %u opcodes", unsigned(info.handled.size()));
	}

	if (func_t* f = get_func(first.dispatcher))
		set_func_cmt(f, c.c_str(), false);
	mark_cfunc_dirty(first.dispatcher);

	// Name the actual switch func when the dispaher is a wrapper
	if (first.switch_func != BADADDR && first.switch_func != first.dispatcher &&
		!has_user_name(get_flags(first.switch_func))) {
		set_name(
			first.switch_func, "vst2_dispatcher_impl", SN_NOWARN | SN_FORCE);
		mark_cfunc_dirty(first.switch_func);
	}

	rv_log("  %s", c.c_str());
	prepare_report(infos);
}

} // namespace rv::vst2
