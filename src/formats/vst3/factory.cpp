#include "factory.hpp"
#include "core/log.hpp"
#include "db/read.hpp"
#include <cstring>
#include <set>
#include <typeinf.hpp>
#include <utility>

namespace rv::vst3 {

namespace {

constexpr uint32 MANY_INSTANCES = 0x7FFFFFFF;

constexpr const char* FACTORY_INFO_MEMBERS[] = { "vendor", "url", "email",
	"flags" };

constexpr const char* CLASS_INFO_MEMBERS[] = { "cid", "cardinality", "category",
	"name", "classFlags", "subCategories", "vendor", "version", "sdkVersion" };

struct layout_t {
	std::vector<field_desc_t> fields;
	int size = 0;

	const field_desc_t* find(const char* name) const {
		for (const field_desc_t& f : fields)
			if (strcmp(f.name, name) == 0) return &f;
		return nullptr;
	}
};

template <size_t N>
bool load_layout(
	const char* type, const char* const (&members)[N], layout_t& out) {
	tinfo_t t;
	if (!t.get_named_type(get_idati(), type) || !t.is_struct()) {
		rv_log("%s not found in local types - was types.h parsed?", type);
		return false;
	}
	out.size = int(t.get_size());

	for (const char* name : members) {
		udm_t udm;
		udm.name = name;
		if (t.find_udm(&udm, STRMEM_NAME) < 0) {
			rv_log("%s::%s missing from types.h", type, name);
			return false;
		}
		const fkind_t k = udm.type.is_array() ? F_STR : F_INT;
		out.fields.push_back(
			{ name, int(udm.offset / 8), int(udm.size / 8), k });
	}
	return true;
}

/*
	Adds stores to globals, and the inlined strncpy8 calls.
	A copy is kept with size 0 until a layout tells how wide its field is
*/
struct factory_collector_t : public store_collector_t {
	std::vector<store_t> copies;

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

	void add_copy(cexpr_t* e) {
		store_t c;

		cexpr_t* dst = strip_casts(e->y);
		if (dst->op == cot_ref) dst = strip_casts(dst->x);

		if (dst->op == cot_obj)
			c.loc = loc_t{ GLOBAL_ROOT, int64(dst->obj_ea) };
		else if (!fold_addr(e->y, &alias, c.loc))
			return;

		// Stays UNKNOWN for a string built at runtime
		cexpr_t* src = strip_casts(e->x);
		if (src->op == cot_ref) src = strip_casts(src->x);

		if (src->op == cot_obj) {
			c.val.t = val_t::ADDR;
			c.val.ea = src->obj_ea;
		}

		c.ea = e->ea;
		c.text = expr_str(e, cf);
		copies.push_back(std::move(c));
	}

	int idaapi visit_expr(cexpr_t* e) override {
		if (e->op == cot_sub) {
			add_copy(e);
			return 0;
		}
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
		auto c = h.fields.find("category");
		if (c == h.fields.end() || c->second.v.t != val_t::ADDR ||
			!read_utf8(c->second.v.ea, category) || category.empty()) {
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

	rv_log("Factory scan %a: %u foldable stores, %u string copies",
		cf->entry_ea, unsigned(col.stores.size()), unsigned(col.copies.size()));

	find_class_infos(col, out);
	find_factory_infos(col, out);
	return out;
}

} // namespace rv::vst3
