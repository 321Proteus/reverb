#include "factory.hpp"
#include "core/log.hpp"
#include "db/read.hpp"
#include "db/types.hpp"
#include "decomp/call.hpp"
#include <bytes.hpp>
#include <cstring>
#include <set>
#include <typeinf.hpp>
#include <utility>

namespace rv::vst3 {

void tuid_from_longs(const uint32 (&l)[4], bool com, uchar (&out)[16]) {
	for (int w = 0; w < 4; w++)
		for (int k = 0; k < 4; k++)
			out[4 * w + k] = uchar(l[w] >> (24 - 8 * k));
	if (!com) return;
	std::swap(out[0], out[3]);
	std::swap(out[1], out[2]);
	std::swap(out[4], out[5]);
	std::swap(out[6], out[7]);
}

namespace {

constexpr uint32 MANY_INSTANCES = 0x7FFFFFFF;

constexpr const char* FACTORY_INFO_MEMBERS[] = { "vendor", "url", "email",
	"flags" };

constexpr const char* CLASS_INFO_MEMBERS[] = { "cid", "cardinality", "category",
	"name", "classFlags", "subCategories", "vendor", "version", "sdkVersion" };

// Adds stores to globals
struct factory_collector_t : public store_collector_t {
	std::set<ea_t> globals; // every global the function refers to

	factory_collector_t(const cfunc_t* f) : store_collector_t(f) {}

	void add_global_store(cexpr_t* e, cexpr_t* lhs) {
		const size_t sz = lhs->type.get_size();
		if (sz == BADSIZE) return;

		store_t s;
		s.loc = loc_t{ GLOBAL_ROOT, int64(lhs->obj_ea) };
		s.size = int(sz);
		s.val = value_of(e->y);
		s.ea = e->ea;
		s.text = expr_str(e, cf);

		ea_t src = BADADDR;
		if (s.val.t == val_t::UNKNOWN && const_load(e->y, src))
			read_const(src, s.size, s.val);

		stores.push_back(std::move(s));
	}

	int idaapi visit_expr(cexpr_t* e) override {

		if (e->op == cot_obj) globals.insert(e->obj_ea);

		if (e->op == cot_asg) {
			cexpr_t* lhs = strip_casts(e->x);
			if (lhs->op == cot_obj) {
				add_global_store(e, lhs);
				return 0;
			}
		}

		return store_collector_t::visit_expr(e);
	}
};

// The collected stores, plus what only the layout can turn into stores
std::vector<store_t> stores_for(const factory_collector_t& col,
	const layout_t& layout, const factory_struct_t& h) {
	std::vector<store_t> out = col.stores;

	for (const field_desc_t& f : layout.fields) {
		const int64 foff = h.base_off + f.off;

		// A field written with runtime values, like the cid byte by
		// byte, would otherwise look unwritten
		store_t pieces;
		for (const store_t& s : col.stores) {
			if (s.loc.var != h.root || s.is_or || s.val.t != val_t::UNKNOWN ||
				s.loc.off < foff || s.loc.off + s.size > foff + f.sz)
				continue;
			if (pieces.ea == BADADDR) pieces.ea = s.ea;
			pieces.size += s.size;
		}
		if (pieces.size >= f.sz) {
			pieces.loc = loc_t{ h.root, foff };
			pieces.size = f.sz;
			out.push_back(std::move(pieces));
		}

		for (const store_t& c : col.copies) {
			if (c.loc.var != h.root || c.loc.off != foff) continue;
			out.push_back(c);
			out.back().size = f.sz;
		}
	}
	return out;
}

qstring base_str(const factory_struct_t& h) {
	qstring s;
	if (h.root == GLOBAL_ROOT) s.sprnt("%a", ea_t(h.base_off));
	else if (h.root == FRAME_ROOT)
		s.sprnt("frame%+lld", (long long)h.base_off);
	else
		s.sprnt("var%d%+lld", h.root, (long long)h.base_off);
	return s;
}

void resolve(const factory_collector_t& col, const layout_t& layout,
	factory_struct_t& h) {
	resolve_fields(
		layout.fields, h, stores_for(col, layout, h), inf_is_64bit());
}

// cardinality is kManyInstances in practice, which makes it the magic
void find_class_infos(
	const factory_collector_t& col, std::vector<factory_struct_t>& out) {

	layout_t layout;
	if (!load_layout("PClassInfo2", CLASS_INFO_MEMBERS, layout)) return;
	const int64 card_off = layout.find("cardinality")->off;

	std::set<std::pair<int, int64>> seen;

	for (const store_t& m : col.stores) {
		if (m.is_or || m.val.t != val_t::NUM || m.size < 4 ||
			uint32(m.val.num) != MANY_INSTANCES)
			continue;

		factory_struct_t h;
		h.type = "PClassInfo2";
		h.root = m.loc.var;
		h.base_off = m.loc.off - card_off;
		if (!seen.insert({ h.root, h.base_off }).second) continue;

		rv_log("  cardinality store at %a: PClassInfo2 at %s", m.ea,
			base_str(h).c_str());
		resolve(col, layout, h);

		// Rejects a plain INT_MAX stored in the same function
		qstring category;
		if (!str_field(h, "category", category) || category.empty()) {
			rv_log("  -> rejected: category is not a constant string");
			continue;
		}
		out.push_back(std::move(h));
	}
}

bool has_copy(const factory_collector_t& col, int root, int64 off) {
	for (const store_t& c : col.copies)
		if (c.loc.var == root && c.loc.off == off) return true;
	return false;
}

store_t raw_store(loc_t at, const uchar* raw, int size, ea_t ea) {
	store_t s = num_store(at, size, 0, ea);
	memcpy(s.val.raw, raw, size);
	s.val.raw_len = size;
	for (int i = 0; i < size && i < 8; i++)
		s.val.num |= uint64(raw[i]) << (8 * i);
	return s;
}

// FUID rebuilt per byte (f(&tmp, l1, l2, l3, l4))
bool match_uid_builder(const std::vector<call_arg_t>& a, uint32 (&l)[4]) {
	if (a.size() != 5 || a[0].k != call_arg_t::VAR) return false;

	for (int i = 1; i < 5; i++)
		if (a[i].k != call_arg_t::NUM) return false;

	for (int i = 0; i < 4; i++)
		l[i] = uint32(a[i + 1].num);

	return true;
}

// A PFactoryInfo that sits in data: vendor is NUL-padded to its full width.
// A pointer to a bare string breaks the check
bool looks_like_factory_info(ea_t ea, const layout_t& info) {
	const field_desc_t *vendor = info.find("vendor"), *url = info.find("url"),
					   *email = info.find("email"), *flags = info.find("flags");
	if (!vendor || !url || !email || !flags) return false;
	if (ea == BADADDR || !is_mapped(ea) || !is_mapped(ea + info.size - 1))
		return false;

	std::vector<uchar> b(info.size);
	if (get_bytes(b.data(), info.size, ea) != info.size) return false;

	const size_t n = strnlen((const char*)&b[vendor->off], vendor->sz);
	if (n == 0 || n == size_t(vendor->sz)) return false;
	for (size_t i = n; i < size_t(vendor->sz); i++)
		if (b[vendor->off + i] != 0) return false;

	return (b[url->off] != 0 || b[email->off] != 0) &&
		   get_dword(ea + flags->off) <= 0x1F; // kUnicode is the top flag
}

// A complete PFactoryInfo in data, passed to the factory constructor or copied
// by an inlined one
void lower_factory_infos(factory_collector_t& col, const layout_t& info) {
	for (const ea_t ea : col.globals) {
		if (!looks_like_factory_info(ea, info)) continue;

		for (const field_desc_t& f : info.fields) {
			const loc_t at{ GLOBAL_ROOT, int64(ea + f.off) };
			if (f.kind == F_STR)
				col.copies.push_back(copy_of(at, ea + f.off, ea));
			else {
				store_t s = num_store(at, f.sz, 0, ea);
				if (read_const(ea + f.off, f.sz, s.val))
					col.stores.push_back(std::move(s));
			}
		}
	}
}

// PClassInfo2 ctor: (this, cid, cardinality, category, name, classFlags,
// subCategories, vendor, version, sdkVersion). Anchored on kManyInstances.
void lower_class_info(factory_collector_t& col, const call_t& c,
	const layout_t& cls, const uint32 (*uid)[4], bool com) {
	const std::vector<call_arg_t>& A = c.args;
	const size_t n = cls.fields.size();

	size_t card = 0; // position of cardinality in the layout
	while (card < n && cls.fields[card].name != "cardinality")
		card++;
	if (card == n) return;

	for (size_t j = card + 1; j < A.size(); j++) {
		if (A[j].k != call_arg_t::NUM || uint32(A[j].num) != MANY_INSTANCES)
			continue;

		const size_t first = j - card; // argument holding cid
		if (first + n > A.size()) return;

		const call_arg_t& self = A[first - 1];
		loc_t base;
		if (self.k == call_arg_t::OBJ)
			base = loc_t{ GLOBAL_ROOT, int64(self.ea) };
		else if (self.k == call_arg_t::VAR)
			base = loc_t{ self.var, 0 };
		else
			return;

		for (size_t i = 0; i < n; i++) {
			const field_desc_t& f = cls.fields[i];
			const call_arg_t& a = A[first + i];
			const loc_t at{ base.var, base.off + f.off };

			if (f.name == "cid") {
				uchar raw[16] = {};
				if (a.k == call_arg_t::OBJ) {
					if (get_bytes(raw, 16, a.ea) != 16) continue;
				} else if (a.k == call_arg_t::VAR && uid != nullptr)
					tuid_from_longs(*uid, com, raw);
				else
					continue;
				col.stores.push_back(raw_store(at, raw, 16, c.ea));
			} else if (f.kind == F_INT && a.k == call_arg_t::NUM)
				col.stores.push_back(num_store(at, f.sz, a.num, c.ea));
			else if (f.kind == F_STR && a.k == call_arg_t::OBJ)
				col.copies.push_back(copy_of(at, a.ea, c.ea));
			else if (f.kind == F_STR && a.k == call_arg_t::STR)
				rv_log("    %s at %a is a cot_str with no address",
					f.name.c_str(), c.ea);
			// NUM 0 for a string is a NULL pointer: leave the field unwritten
		}
		return;
	}
}

// Calls are in visit order, so the uid in effect is the last builder before
// each constructor
void lower_call_args(
	factory_collector_t& col, const std::vector<call_t>& calls) {
	layout_t cls, info;
	const bool have_cls = load_layout("PClassInfo2", CLASS_INFO_MEMBERS, cls);
	const bool have_info =
		load_layout("PFactoryInfo", FACTORY_INFO_MEMBERS, info);
	const bool com = inf_get_filetype() == f_PE;

	uint32 uid[4] = {};
	bool have_uid = false;

	if (have_info) lower_factory_infos(col, info);

	for (const call_t& c : calls) {
		if (match_uid_builder(c.args, uid)) have_uid = true;
		if (have_cls)
			lower_class_info(col, c, cls, have_uid ? &uid : nullptr, com);
	}
}

/*
	PFactoryInfo has no magic, but three strings at fixed distances are rare.
	The strings of consecutive PClassInfo2s can line up the same way, so
   anything inside the class infos found so far doesn't count
*/
void find_factory_infos(
	const factory_collector_t& col, std::vector<factory_struct_t>& out) {

	layout_t classes;
	if (!load_layout("PClassInfo2", CLASS_INFO_MEMBERS, classes)) return;

	layout_t layout;
	if (!load_layout("PFactoryInfo", FACTORY_INFO_MEMBERS, layout)) return;
	const int64 url_off = layout.find("url")->off;
	const int64 email_off = layout.find("email")->off;

	const std::vector<factory_struct_t> known = out;
	const auto in_class = [&](const store_t& c) {
		for (const factory_struct_t& k : known)
			if (k.root == c.loc.var && c.loc.off >= k.base_off &&
				c.loc.off < k.base_off + classes.size)
				return true;
		return false;
	};

	std::set<std::pair<int, int64>> seen;

	for (const store_t& c : col.copies) {
		if (in_class(c) || !has_copy(col, c.loc.var, c.loc.off + url_off) ||
			!has_copy(col, c.loc.var, c.loc.off + email_off) ||
			!seen.insert({ c.loc.var, c.loc.off }).second)
			continue;

		factory_struct_t h;
		h.type = "PFactoryInfo";
		h.root = c.loc.var;
		h.base_off = c.loc.off;

		rv_log("  PFactoryInfo at %s", base_str(h).c_str());
		resolve(col, layout, h);
		out.push_back(std::move(h));
	}
}

} // namespace

std::vector<factory_struct_t> extract_factory_structs(cfunc_t* cf) {
	std::vector<factory_struct_t> out;
	if (cf == nullptr) return out;

	factory_collector_t col(cf);
	col.apply_to(&cf->body, nullptr);

	call_collector_t cc;
	cc.apply_to(&cf->body, nullptr);

	rv_log("Factory scan %a: %u foldable stores, %u string copies, %u calls",
		cf->entry_ea, unsigned(col.stores.size()), unsigned(col.copies.size()),
		unsigned(cc.calls.size()));

	lower_call_args(col, cc.calls);

	find_class_infos(col, out);
	find_factory_infos(col, out);

	// A PFactoryInfo on the stack, split into one lvar per member
	factory_collector_t frame(cf);
	frame.stores = frame_view(cf, col.stores);
	frame.copies = frame_view(cf, col.copies);
	find_factory_infos(frame, out);
	return out;
}

bool str_field(const factory_struct_t& s, const char* f, qstring& out) {
	auto it = s.fields.find(f);

	if (it == s.fields.end()) return false;

	const val_t& v = it->second.v;
	if (v.t == val_t::STR) out = v.str;
	return v.t == val_t::STR || (v.t == val_t::ADDR && read_utf8(v.ea, out));
}

bool num_field(const factory_struct_t& s, const char* f, uint64& out) {
	bool have = false;
	out = 0;

	if (auto it = s.fields.find(f);
		it != s.fields.end() && it->second.v.t == val_t::NUM) {
		have = true;
		out = it->second.v.num;
	}
	if (auto ob = s.or_bits.find(f); ob != s.or_bits.end()) {
		have = true;
		out |= ob->second;
	}
	return have;
}

} // namespace rv::vst3
