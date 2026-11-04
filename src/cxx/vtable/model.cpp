#include "model.hpp"
#include "db/read.hpp"
#include "layout.hpp"
#include <algorithm>
#include <allins.hpp>
#include <funcs.hpp>
#include <idp.hpp>
#include <name.hpp>
#include <tuple>
#include <ua.hpp>

namespace rv::vtbl {

constexpr uint16 R_CX = 1, R_SP = 4, R_BP = 5, R_DI = 7;

static bool is_reg(const op_t& o, uint16 r) {
	return o.type == o_reg && o.reg == r;
}

// Immediate or displacement as by operand size
static sval_t signed_val(const op_t& o, uval_t v) {
	switch (get_dtype_size(o.dtype)) {
	case 1:	 return sval_t(int8(v));
	case 2:	 return sval_t(int16(v));
	case 4:	 return sval_t(int32(v));
	default: return sval_t(v);
	}
}

/*
	this += delta: add/sub rcx/rdi/ecx, imm; lea rcx, [rcx+imm]; stdcall
	add/sub dword ptr [esp+4], imm ([esp+8] or [ebp+8] inside a frame).
	specflag1 = has SIB, specflag2 = SIB byte (index 4 = none)
*/
static bool this_adjust(const insn_t& in, bool frame, sval_t& delta) {
	const bool x64 = inf_is_64bit();
	auto this_reg = [&](const op_t& o) {
		return is_reg(o, R_CX) || (x64 && is_reg(o, R_DI));
	};

	auto stack_this = [&](const op_t& o) {
		if (x64 || o.type != o_displ) return false;

		if (o.specflag1 && ((o.specflag2 >> 3) & 7) != 4) return false;

		const uint16 base = o.specflag1 ? (o.specflag2 & 7) : o.phrase;
		return (base == R_SP && o.addr == (frame ? 8 : 4)) ||
			   (frame && base == R_BP && o.addr == 8);
	};

	const op_t& d = in.ops[0];
	const op_t& s = in.ops[1];

	if ((in.itype == NN_add || in.itype == NN_sub) && s.type == o_imm &&
		(this_reg(d) || stack_this(d))) {

		const sval_t v = signed_val(s, s.value);
		delta = in.itype == NN_add ? v : -v;

		return true;
	}

	if (in.itype == NN_lea && this_reg(d) && s.type == o_displ &&
		!s.specflag1 && s.phrase == d.reg) {
		delta = x64 ? sval_t(s.addr) : sval_t(int32(s.addr));
		return true;
	}

	return false;
}

bool adjustor_thunk(ea_t ea, ea_t* target, sval_t* delta) {
	if (PH.id != PLFM_386) return false;

	bool frame = false, adjusted = false;
	sval_t d = 0;
	insn_t in;

	for (int n = 0; n < 8; n++, ea += in.size) {
		if (decode_insn(&in, ea) <= 0) return false;
		const op_t& a = in.ops[0];

		if (!adjusted) {
			if (n == 0 && (in.itype == NN_endbr64 || in.itype == NN_endbr32))
				continue;

			if (!frame && in.itype == NN_push && is_reg(a, R_BP)) {
				frame = true;
				continue;
			}

			if (frame && in.itype == NN_mov && is_reg(a, R_BP) &&
				is_reg(in.ops[1], R_SP))
				continue;

			if (!this_adjust(in, frame, d)) return false;
			adjusted = true;

			continue;
		}

		if (frame && in.itype == NN_pop && is_reg(a, R_BP)) {
			frame = false;
			continue;
		}

		if (frame || in.itype != NN_jmp || a.type != o_near) return false;

		if (target) *target = a.addr;
		if (delta) *delta = d;

		return true;
	}

	return false;
}

static bool fits(const tinfo_t& t, size_t extent) {
	const size_t sz = t.get_size();
	return sz != BADSIZE && sz != 0 && sz / ptr_size() <= extent;
}

static bool vtbl_type(const qstring& iface, tinfo_t& out) {
	return out.get_named_type(nullptr, (iface + "_vtbl").c_str()) &&
		   out.is_struct();
}

bool model::add(
	ea_t vt, const char* forced, const std::vector<const char*>& fallback) {

	table_rec* r;
	auto it = index_.find(vt);

	if (it != index_.end()) {
		r = &vtables[it->second];
	} else {
		table_rec rec;
		rec.ea = vt;
		rec.extent = extent(vt);
		if (rec.extent == 0) return false;

		rtti::parse_rtti(vt, &rec.cls);
		if (!rec.cls.name.empty()) classes.emplace(rec.cls.name, rec.cls);

		index_[vt] = vtables.size();
		vtables.push_back(std::move(rec));
		r = &vtables.back();
	}

	auto take = [&](const qstring& iface) {
		tinfo_t t;
		if (!vtbl_type(iface, t)) return false;
		r->iface = iface;
		r->iface_vtbl = t;
		return true;
	};

	if (forced != nullptr) {
		qstring iface = forced;
		if (iface.ends_with("_vtbl")) iface.resize(iface.length() - 5);
		return take(iface);
	}
	if (!r->iface.empty()) return true;

	// Largest generated interface among the bases served by this vtable
	qstring best;
	size_t best_size = 0;
	for (const qstring& b : rtti::bases_at(r->cls, r->cls.sub_offset)) {
		const qstring s = rtti::short_name(b);
		tinfo_t t;
		if (vtbl_type(s, t) && fits(t, r->extent) && t.get_size() > best_size) {
			best = s;
			best_size = t.get_size();
		}
	}

	// Abstract interface vtables hold pure virtual stubs only
	if (!best.empty() && best == rtti::short_name(r->cls.name)) return true;

	if (best.empty()) {
		for (const char* c : fallback) {
			tinfo_t t;
			if (t.get_named_type(nullptr, c) && fits(t, r->extent)) {
				best = c;
				if (best.ends_with("_vtbl")) best.resize(best.length() - 5);
				break;
			}
		}
	}

	if (!best.empty()) take(best);
	return true;
}

size_t model::add_rtti_vtables() {
	const int psz = ptr_size();
	size_t n = 0;

	auto add_rtti = [&](ea_t vt) {
		rtti::class_info ci;
		if (!rtti::parse_rtti(vt, &ci) || !add(vt)) return false;
		n++;
		return true;
	};

	for (size_t i = 0; i < get_nlist_size(); i++) {
		const char* name = get_nlist_name(i);
		const ea_t ea = get_nlist_ea(i);
		if (name == nullptr || ea == BADADDR) continue;

		if (strncmp(name, "??_7", 4) == 0) {
			add_rtti(ea);
			continue;
		}

		if (name[0] == '_' && name[1] == '_') name++; // Mach-O
		if (strncmp(name, "_ZTV", 4) != 0) continue;

		/*
			A _ZTV group holds the primary vtable and then each secondary
			one: offset-to-top (negative), the same typeinfo, slots
		*/
		ea_t ap = ea + 2 * psz;
		const ea_t ti = read_ptr(ap - psz);
		while (add_rtti(ap)) {
			const ea_t next = ap + extent(ap) * psz;
			const sval_t ott = psz == 8 ? sval_t(int64(get_qword(next)))
										: sval_t(int32(get_dword(next)));
			if (next == ap || ott >= 0 || read_ptr(next + psz) != ti) break;
			ap = next + 2 * psz;
		}
	}
	return n;
}

bool model::is_base_of(const qstring& base, const qstring& derived) const {
	auto it = classes.find(derived);
	if (it == classes.end()) return false;

	for (const rtti::base_info& b : it->second.bases)
		if (b.name == base) return true;
	return false;
}

void model::collect() {
	slots.clear();
	funcs.clear();

	for (const table_rec& r : vtables) {
		if (r.iface_vtbl.empty()) continue;

		for_each_member(r.iface_vtbl, [&](const udm_t& m, size_t off) {
			const size_t idx = off / ptr_size();
			if (idx >= r.extent) return;

			slot_rec s;
			s.func = read_slot(r.ea, idx);
			if (!is_slot_target(s.func)) return;

			s.vt = r.ea;
			s.index = idx;
			s.cls = r.cls.name.empty() ? r.iface : r.cls.name;
			s.iface = r.iface;
			s.method = m.name;
			s.sub_offset = r.cls.sub_offset;
			s.type = m.type;
			slots.push_back(s);

			// The thunk's target is labeled from the same slot and adjust this
			ea_t target;
			sval_t delta;

			if (adjustor_thunk(s.func, &target, &delta) &&
				is_slot_target(target)) {
				s.func = target;
				s.sub_offset += delta;
				s.via_thunk = true;
				slots.push_back(s);
			}
		});
	}

	std::map<ea_t, std::vector<int>> by_func;
	for (size_t i = 0; i < slots.size(); i++)
		by_func[slots[i].func].push_back(int(i));

	for (const auto& [f, recs] : by_func)
		classify(f, recs);

	assign_names();
}

// Record for owner (any if null): direct before via thunk, primary before
// secondary, then by address so the choice doesn't depend on discovery order
static int pick_rec(const std::vector<slot_rec>& slots,
	const std::vector<int>& recs, const qstring* owner) {
	auto key = [&](int i) {
		const slot_rec& s = slots[i];
		return std::make_tuple(s.via_thunk, s.sub_offset != 0, s.vt, s.index);
	};
	int best = -1;
	for (int i : recs) {
		if ((owner == nullptr || slots[i].cls == *owner) &&
			(best < 0 || key(i) < key(best)))
			best = i;
	}

	return best;
}

/*
	The class a function shared by recs is named after: the only owner, an
	owner that is a base of all the others, or else the deepest base all
	owners share at their slot offsets (siblings inheriting one method)
*/
bool model::resolve_owner(const std::vector<int>& recs, func_rec& fr) const {
	std::set<qstring> owners;
	for (int i : recs)
		owners.insert(slots[i].cls);

	for (const qstring& a : owners) {
		bool all = true;
		for (const qstring& b : owners)
			if (a != b && !is_base_of(a, b)) all = false;
		if (!all) continue;

		fr.owner = a;
		fr.rec = pick_rec(slots, recs, &a);
		fr.this_off = slots[fr.rec].sub_offset;
		return true;
	}

	// Bases at the slot offsets per owner; the first owner's keep their
	// order, most derived first
	std::vector<qstring> common;
	std::vector<std::set<qstring>> others;
	bool first = true;
	for (const qstring& o : owners) {
		auto c = classes.find(o);
		if (c == classes.end()) return false;

		std::vector<qstring> at;
		for (int i : recs) {
			if (slots[i].cls != o) continue;
			for (const qstring& b :
				rtti::bases_at(c->second, slots[i].sub_offset))
				if (std::find(at.begin(), at.end(), b) == at.end())
					at.push_back(b);
		}

		if (first) common = at;
		else
			others.emplace_back(at.begin(), at.end());

		first = false;
	}

	common.erase(std::remove_if(common.begin(), common.end(),
					 [&](const qstring& b) {
						 for (const auto& s : others)
							 if (s.count(b) == 0) return true;
						 return false;
					 }),
		common.end());

	// Deepest: not a base of another shared base (RTTI permitting), order
	// breaks ties
	for (const qstring& c : common) {
		bool deepest = true;
		for (const qstring& d : common)
			if (d != c && is_base_of(c, d)) deepest = false;
		if (!deepest) continue;

		fr.owner = c;
		fr.rec = pick_rec(slots, recs, nullptr);
		fr.this_off = 0; // the base sits at the slot's offset
		return true;
	}
	return false;
}

void model::classify(ea_t f, const std::vector<int>& recs) {
	func_rec fr;
	fr.refs = slot_refs(f);

	std::set<qstring> owners, methods, thunked;
	int direct = 0;
	for (int i : recs) {
		owners.insert(slots[i].cls);
		methods.insert(slots[i].method);
		if (slots[i].via_thunk) thunked.insert(slots[i].cls);
		else
			direct++;
	}

	// A thunk target also has a slot in the untyped part of its class's
	// primary vtable (GCC/Clang), one per class at most
	const int extra = fr.refs - direct;

	const qstring name = get_name(f);
	func_t* pf = get_func(f);

	if (name.find("purecall") != qstring::npos ||
		name.find("__cxa_pure_virtual") != qstring::npos) {
		fr.kind = func_kind::PURECALL;
	} else if (adjustor_thunk(f, &fr.thunk_target)) {
		fr.kind = func_kind::THUNK;
		if (methods.size() == 1) resolve_owner(recs, fr);
	} else if (pf != nullptr &&
			   (pf->start_ea != f || (pf->flags & FUNC_LIB) != 0)) {
		fr.kind = func_kind::SKIP;
	} else if (extra < 0 || extra > int(thunked.size()) ||
			   methods.size() != 1 || !resolve_owner(recs, fr)) {
		// Slots elsewhere (other classes, unknown vtables), other methods or
		// owners without a shared base
		fr.kind = func_kind::FOLDED;
		fr.rec = -1;
		fr.owner.clear();
	} else {
		fr.kind = owners.size() == 1 ? func_kind::OWN : func_kind::INHERITED;
	}

	funcs[f] = fr;
}

/*
	"owner::method", "$thunk<off>" for thunks. Functions with the same name
	are entry points of one method (GCC emits this-adjusted copies that are
	not thunks): the lowest this offset keeps the name, the others get
	"$adj<off>", offsets in hex.
*/
void model::assign_names() {
	std::map<qstring, std::vector<ea_t>> by_name;

	for (auto& [f, fr] : funcs) {
		if (fr.rec < 0 || fr.owner.empty()) continue;
		const slot_rec& s = slots[fr.rec];

		fr.name.sprnt("%s::%s", fr.owner.c_str(), s.method.c_str());
		if (fr.kind == func_kind::THUNK)
			fr.name.cat_sprnt("$thunk%X", uint32(s.sub_offset));
		by_name[fr.name].push_back(f);
	}

	std::set<qstring> taken;
	for (const auto& [n, fs] : by_name)
		taken.insert(n);

	for (auto& [n, fs] : by_name) {
		if (fs.size() < 2) continue;

		auto key = [&](ea_t f) {
			const slot_rec& s = slots[funcs[f].rec];
			return std::make_tuple(s.sub_offset, s.via_thunk, s.vt, s.index);
		};
		std::sort(fs.begin(), fs.end(),
			[&](ea_t a, ea_t b) { return key(a) < key(b); });

		for (size_t i = 1; i < fs.size(); i++) {
			func_rec& fr = funcs[fs[i]];
			qstring alias;
			alias.sprnt(
				"%s$adj%X", n.c_str(), uint32(slots[fr.rec].sub_offset));
			for (int k = 2; taken.count(alias) != 0; k++)
				alias.sprnt("%s$adj%X$%d", n.c_str(),
					uint32(slots[fr.rec].sub_offset), k);
			taken.insert(alias);
			fr.name = alias;
		}
	}
}

} // namespace rv::vtbl
