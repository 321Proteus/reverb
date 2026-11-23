#pragma once
#include "core/log.hpp"
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <vector>
#include <pro.h>
#include <typeinf.hpp>

/**
 * @file type_name.hpp
 * @brief Finds a type by its unqualified name, whichever way it got into the
 * database.
 *
 * - from a til parsed by clang: `Steinberg::PFactoryInfo`
 * - from the flat decl fallback: `PFactoryInfo`
 *
 * A format searches only its own namespaces (a TypeScope), so a VST3 lookup
 * can never land on a `juce::` type that happens to share the short name.
 */
namespace rv {

/** @brief A constexpr view of a static array of prefixes */
struct PrefixList {
	const char* const* first;
	size_t count;

	constexpr const char* const* begin() const { return first; }
	constexpr const char* const* end() const { return first + count; }

	constexpr bool empty() const { return count == 0; }
	constexpr const char* operator[](size_t i) const { return first[i]; }
};

/** @brief Wraps a static array of prefixes in a PrefixList */
template <size_t N>
constexpr PrefixList make_prefixes(const char* const (&a)[N]) {
	return { a, N };
}

/**
 * @brief The namespaces one format may look types up in
 */
struct TypeScope {
	const char* id;		   /**< Cache key */
	PrefixList prefixes; /**< Tried in order, `""` is the flat name; the first is also what resolve_type_or_default() falls back to */
};

inline constexpr const char* VST3_PREFIXES[] = {
	"Steinberg::", "Steinberg::Vst::", ""
};

/** @brief Scope of the VST3 types */
inline constexpr TypeScope VST3_SCOPE{ "vst3", make_prefixes(VST3_PREFIXES) };

namespace detail {

inline std::map<std::string, std::string>& type_cache() {
	static std::map<std::string, std::string> m;
	return m;
}

inline std::set<std::string>& ambiguous_logged() {
	static std::set<std::string> s;
	return s;
}

inline bool type_exists(const char* name) {
	tinfo_t t;
	return t.get_named_type(nullptr, name);
}

/** @brief `"Steinberg::"` for `"Steinberg::Vst::"`; empty for `""` */
inline std::string root_of(const char* prefix) {
	const char* sep = std::strstr(prefix, "::");
	return sep ? std::string(prefix, size_t(sep - prefix) + 2) : std::string();
}

} // namespace detail

/**
 * @brief Drops cached lookups
 *
 * Call when types may have been added or the database changed (til loaded,
 * decls parsed, database closed). Only hits are cached, so a type that appears
 * later is still found; this only drops answers that went stale.
 */
inline void reset_type_cache() {
	detail::type_cache().clear();
	detail::ambiguous_logged().clear();
}

/**
 * @brief Full name of the type @p base within @p sc
 *
 * @return The name, empty if there is none or it is ambiguous
 */
inline qstring resolve_type(const TypeScope& sc, const char* base) {
	const std::string key = std::string(sc.id) + '|' + base;

	auto& cache = detail::type_cache();
	if (auto it = cache.find(key); it != cache.end())
		return qstring(it->second.c_str());

	qstring name;
	for (const char* p : sc.prefixes) {
		name.sprnt("%s%s", p, base);
		if (detail::type_exists(name.c_str())) {
			cache[key] = name.c_str();
			return name;
		}
	}

	/*
		Nested deeper than the prefixes know (Steinberg::Vst::Foo::Bar):
		search the local types for <root>...::<base>. Types that live only in
		a base til are not numbered, so til::load imports them first
	*/
	std::vector<std::string> roots;
	for (const char* p : sc.prefixes) {
		std::string r = detail::root_of(p);
		if (!r.empty()) roots.push_back(std::move(r));
	}

	if (roots.empty()) return qstring();

	const size_t blen = std::strlen(base);
	qstring found;
	bool ambiguous = false;
	const uint32 lim = get_ordinal_limit();

	for (uint32 ord = 1; ord < lim; ord++) {

		const char* n = get_numbered_type_name(nullptr, ord);
		if (n == nullptr) continue;

		const size_t nlen = std::strlen(n);
		if (nlen <= blen + 2 || std::strcmp(n + nlen - blen, base) != 0 ||
			n[nlen - blen - 1] != ':' || n[nlen - blen - 2] != ':')
			continue;

		bool in_scope = false;
		for (const std::string& r : roots)
			if (std::strncmp(n, r.c_str(), r.size()) == 0) in_scope = true;

		if (!in_scope) continue;

		if (!found.empty()) {
			if (detail::ambiguous_logged().insert(key).second)
				rv_log("Type %s is ambiguous: %s, %s", base, found.c_str(), n);
			ambiguous = true;
			break;
		}

		found = n;
	}

	if (ambiguous || found.empty()) return qstring();

	cache[key] = found.c_str();
	return found;
}

/**
 * @brief resolve_type(), or `<first prefix><base>` when nothing is found so
 * that a failed lookup still logs a qualified name
 */
inline qstring resolve_type_or_default(const TypeScope& sc, const char* base) {
	qstring r = resolve_type(sc, base);
	if (r.empty())
		r.sprnt("%s%s", sc.prefixes.empty() ? "" : sc.prefixes[0], base);

	return r;
}

/** @brief resolve_type_or_default() in VST3_SCOPE */
inline qstring vst3_type(const char* base) {
	return resolve_type_or_default(VST3_SCOPE, base);
}

} // namespace rv