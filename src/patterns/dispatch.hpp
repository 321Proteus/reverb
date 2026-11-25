#pragma once

#include "core/log.hpp"
#include "decomp/ctree.hpp"
#include "decomp/analysis.hpp"
#include "decomp/stores.hpp"
#include "cxx/vtable.hpp"
#include <algorithm>
#include <functional>
#include <hexrays.hpp>
#include <map>
#include <set>
#include <utility>
#include <vector>

/**
 * @file dispatch.hpp
 * @brief Selector dispatch: an entry point that takes a selector (VST2 opcode,
 * AU selector, ...) and reaches per-selector code through a switch, an if-chain
 * and any number of forwarding calls, direct or virtual. Format modules only
 * supply argument positions and decide what to do with each case.
 */
namespace rv {

/**
 * @brief Argument positions of a dispatching function, -1 for absent ones
 */
struct dispatch_args_t {
	int sel = -1;		   /**< Selector */
	int ptr = -1;		   /**< Data pointer */
	int obj = -1;		   /**< The object itself (`this`) */
	int obj_src = -1;	   /**< Object = `*(arg obj_src + obj_off)` */
	int64 obj_off = -1;	   /**< See #obj_src */
	ea_t vtable = BADADDR; /**< Most derived vtable of the object (if known) */

	/** @brief Arguments the function must have to be walked */
	int min_args() const { return 1 + std::max({ sel, ptr, obj, obj_src }); }
};

/**
 * @brief Where a dispatch walk starts and how far it goes
 */
struct dispatch_cfg_t {
	dispatch_args_t root;	 /**< Arguments of the entry function */
	uint64 max_sel = 0xFFFF; /**< Larger selectors are ignored */
	int max_depth = 4;		 /**< Forwarding calls followed */
};

/**
 * @brief One selector case
 *
 * Points into the decompiled function, so it is only valid during the
 * callback.
 */
struct dispatch_case_t {
	uint64 sel;					  /**< Selector value */
	cinsn_t* body;				  /**< Code of the case */
	cfunc_t* cf;				  /**< Function containing it */
	int depth;					  /**< Forwarding calls followed to get here */
	const obj_tracker_t& obj;	  /**< The object, tracked through @c cf */
	const std::set<int>& ptrvars; /**< Locals holding the data pointer */
};

/**
 * @brief What a dispatch walk visited
 */
struct dispatch_result_t {
	ea_t switch_func =
		BADADDR;			 /**< First function with a real selector switch */
	std::vector<ea_t> chain; /**< Visited functions, outermost first */
};

/** @brief Callback receiving each case found */
typedef std::function<void(const dispatch_case_t&)> dispatch_cb_t;

/** @cond INTERNAL */
namespace detail {

/**
 * @brief Whether @p e is the selector (one of @p vars), possibly biased
 *
 * @param bias Receives the value to add to a compared constant to get the
 * selector
 */
inline bool is_sel_expr(cexpr_t* e, const std::set<int>& vars, int64& bias) {
	e = strip_casts(e);
	bias = 0;
	if (e->op == cot_add || e->op == cot_sub) {
		cexpr_t* n = strip_casts(e->y);
		if (n->op != cot_num) return false;
		const int64 k = int64(n->numval());
		bias = (e->op == cot_sub) ? k : -k;
		e = strip_casts(e->x);
	}
	return e->op == cot_var && vars.count(e->v.idx) != 0;
}

/**
 * @brief Selector values for which a condition holds: `sel == N` and `||`
 * chains of them
 *
 * With @p negated set, the values make it false instead (`sel != N` and `&&`
 * chains of them), so they take the else branch.
 *
 * @param c       Condition
 * @param vars    Locals holding the selector
 * @param vals    Receives the selector values
 * @param negated Receives whether the values make the condition false
 * @return false if the condition is not about the selector
 */
inline bool sel_cond(cexpr_t* c, const std::set<int>& vars,
	std::vector<uint64>& vals, bool& negated) {
	c = strip_casts(c);

	if (c->op == cot_lor || c->op == cot_land) {
		bool nx, ny;
		if (!sel_cond(c->x, vars, vals, nx) || !sel_cond(c->y, vars, vals, ny))
			return false;
		negated =
			(c->op == cot_land); // a != 1 && a != 2  ==  !(a == 1 || a == 2)
		return nx == negated && ny == negated;
	}

	if (c->op != cot_eq && c->op != cot_ne) return false;

	cexpr_t* a = strip_casts(c->x);
	cexpr_t* b = strip_casts(c->y);
	if (a->op == cot_num) std::swap(a, b); // allow 45 == a2

	int64 bias;
	if (b->op != cot_num || !is_sel_expr(a, vars, bias)) return false;

	vals.push_back(b->numval() + uint64(bias));
	negated = (c->op == cot_ne);
	return true;
}

/**
 * @brief Finds the case bodies of one function and the calls that forward the
 * selector
 */
struct sel_walker_t : public ctree_visitor_t {
	std::set<int> selvars; /**< Selector arg + copies */
	std::set<int> ptrvars; /**< Data pointer arg + copies */
	obj_tracker_t obj;
	uint64 max_sel = 0;

	bool found_switch = false;
	std::map<uint64, cinsn_t*> bodies; /**< Selector -> case body */

	/** @brief A call that forwards the selector */
	struct fwd_t {
		ea_t callee; /**< Resolved target or BADADDR if unknown */
		int slot;	 /**< Vtable slot of a virtual call, -1 for direct */
		dispatch_args_t args; /**< Where the arguments land in the callee */
		tinfo_t site;		  /**< Prototype at the call site */
	};
	std::vector<fwd_t> fwds;

	sel_walker_t() : ctree_visitor_t(CV_FAST) {}

	/** @brief Records @p body for @p sel; the first one wins, larger selectors
	 * are ignored */
	void add_body(uint64 sel, cinsn_t* body) {
		if (sel <= max_sel && body != nullptr)
			bodies.emplace(sel, body); // first one wins
	}

	int idaapi visit_insn(cinsn_t* i) override {
		if (i->op == cit_switch) {
			int64 bias;
			if (!is_sel_expr(&i->cswitch->expr, selvars, bias)) return 0;
			found_switch = true;
			for (ccase_t& c : i->cswitch->cases)
				for (uint64 v : c.values) // empty values = default case
					add_body(v + uint64(bias), &c);
			return 0;
		}

		if (i->op == cit_if) {
			std::vector<uint64> vals;
			bool negated;
			if (!sel_cond(&i->cif->expr, selvars, vals, negated)) return 0;

			// if (sel == N) { body }  /  if (sel != N) { ... } else { body }
			for (uint64 v : vals)
				add_body(v, negated ? i->cif->ielse : i->cif->ithen);
		}
		return 0;
	}

	int idaapi visit_expr(cexpr_t* e) override {
		if (e->op == cot_asg) {
			absorb_copy(e, selvars);
			absorb_copy(e, ptrvars);
			obj.absorb(e);
			return 0;
		}

		const int sel = arg_pos(e, selvars);
		if (sel < 0) return 0;

		fwd_t f{ BADADDR, -1, {}, e->x->type };
		f.args.sel = sel;
		f.args.ptr = arg_pos(e, ptrvars);

		// `this` is the first argument on every ABI
		cexpr_t* fn = strip_casts(e->x);
		cexpr_t* a0 = e->a->empty() ? nullptr : &e->a->at(0);
		if (fn->op == cot_obj) {
			f.callee = fn->obj_ea;
			if (obj.is_object(a0)) { // a method of the same object
				f.args.obj = 0;
				f.args.vtable = obj.vtable;
			} else if (obj.is_part(a0)) { // of a member, vtable unknown
				f.args.obj = 0;
			}
		} else if (obj.vcall_slot(fn, f.slot)) {
			f.callee = vtbl::slot_func(obj.vtable, f.slot);
			f.args.obj = 0;
			f.args.vtable = obj.vtable;
		} else {
			return 0;
		}
		fwds.push_back(f);
		return 0;
	}
};

/**
 * @brief Walks the function at @p ea, then the calls that forward the selector
 *
 * @param ea      Function
 * @param args    Argument positions in @p ea
 * @param site    Prototype at the call site, used to fix up the arguments
 * @param depth   Forwarding calls followed so far
 * @param cfg     Walk configuration
 * @param seen    Functions already walked
 * @param res     Receives what was visited
 * @param on_case Receives each case
 */
inline void walk(ea_t ea, const dispatch_args_t& args, const tinfo_t* site,
	int depth, const dispatch_cfg_t& cfg, std::set<ea_t>& seen,
	dispatch_result_t& res, const dispatch_cb_t& on_case) {
	if (depth > cfg.max_depth || ea == BADADDR) return;
	ea = resolve_thunk(ea);
	if (!seen.insert(ea).second) return;

	cfuncptr_t cf = decompile_with_args(ea, args.min_args(), site);
	if (cf == nullptr) return;

	const int nargs = int(cf->argidx.size());
	auto arg = [&](int pos) {
		return pos >= 0 && pos < nargs ? cf->argidx[pos] : -1;
	};
	if (arg(args.sel) < 0) return;

	sel_walker_t w;
	w.max_sel = cfg.max_sel;
	w.selvars.insert(arg(args.sel));
	if (arg(args.ptr) >= 0) w.ptrvars.insert(arg(args.ptr));
	if (arg(args.obj) >= 0) w.obj.objvars.insert(arg(args.obj));
	w.obj.src_var = arg(args.obj_src);
	w.obj.src_off = args.obj_off;
	w.obj.vtable = args.vtable;
	w.apply_to(&cf->body, nullptr);

	rv_log("Dispatch: %a (depth %d): %u cases%s, %u forwards", ea, depth,
		unsigned(w.bodies.size()), w.found_switch ? " (switch)" : "",
		unsigned(w.fwds.size()));

	res.chain.push_back(ea);
	if (w.found_switch && res.switch_func == BADADDR) res.switch_func = ea;

	// Case bodies point into cf, hand them out while it is alive
	for (auto& [sel, body] : w.bodies)
		on_case({ sel, body, &*cf, depth, w.obj, w.ptrvars });

	for (const auto& f : w.fwds) {
		if (f.callee == BADADDR) {
			rv_log("  virtual forward thru slot %d: %s", f.slot,
				f.args.vtable == BADADDR ? "no vtable known" : "not code");
			continue;
		}
		walk(f.callee, f.args, &f.site, depth + 1, cfg, seen, res, on_case);
	}
}

} // namespace detail
/** @endcond */

/**
 * @brief Walks a selector dispatcher and hands each case to @p on_case
 *
 * Follows switches, if-chains and forwarding calls, direct or virtual, up to
 * `cfg.max_depth`.
 *
 * @param ea      Entry function
 * @param cfg     Argument positions and limits
 * @param on_case Called for each case; the case is only valid during the call
 * @return The visited functions and the first one with a real switch
 */
inline dispatch_result_t walk_dispatch(
	ea_t ea, const dispatch_cfg_t& cfg, const dispatch_cb_t& on_case) {
	dispatch_result_t res;
	std::set<ea_t> seen;
	detail::walk(ea, cfg.root, nullptr, 0, cfg, seen, res, on_case);
	return res;
}

} // namespace rv
