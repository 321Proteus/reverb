#pragma once

#include <cstdio>
#include <string>
#include <vector>

namespace rv::til {

// A <major>.<minor>.<patch> version tag
struct Version {
	int major = 0, minor = 0, patch = 0;
};

inline bool operator<(const Version& a, const Version& b) {
	if (a.major != b.major) return a.major < b.major;
	if (a.minor != b.minor) return a.minor < b.minor;
	return a.patch < b.patch;
}

// "8.0.9", "v8.0.9", "8.0" (patch 0); false if there are fewer than two numbers
inline bool parse_version(const char* s, Version* out) {
	if (*s == 'v' || *s == 'V') s++;
	*out = {};
	return std::sscanf(s, "%d.%d.%d", &out->major, &out->minor, &out->patch) >=
		   2;
}

/*
	One installed til: rv_<name>_v<tag>_<abi>.til for a versioned source
	and optionally -<suffix>
	rv_<name>_<abi>.til for one without versions.
*/
struct TilEntry {
	Version v;
	std::string suffix;
	std::string file; // full path
	std::string name; // file name without extension
};

inline bool starts_with(const std::string& s, const std::string& p) {
	return s.compare(0, p.size(), p) == 0;
}

inline bool ends_with(const std::string& s, const std::string& p) {
	return s.size() >= p.size() &&
		   s.compare(s.size() - p.size(), p.size(), p) == 0;
}

// rv_<name>_<abi>.til
inline bool parse_unversioned_file_name(const std::string& fname,
	const std::string& name, const std::string& abi, TilEntry* out) {

	if (fname != "rv_" + name + "_" + abi + ".til") return false;

	TilEntry e;
	e.name = fname.substr(0, fname.size() - 4);
	*out = e;

	return true;
}

// False if it is not this source's til for abi
inline bool parse_file_name(const std::string& fname, const std::string& fw,
	const std::string& abi, TilEntry* out) {

	const std::string head = "rv_" + fw + "_v";
	const std::string tail = "_" + abi + ".til";

	if (fname.size() <= head.size() + tail.size() ||
		!starts_with(fname, head) || !ends_with(fname, tail))
		return false;

	const std::string tag =
		fname.substr(head.size(), fname.size() - head.size() - tail.size());

	TilEntry e;
	int n = 0;
	if (std::sscanf(tag.c_str(), "%d.%d.%d%n", &e.v.major, &e.v.minor,
			&e.v.patch, &n) < 3)
		return false;

	const std::string rest = tag.substr(size_t(n));
	if (!rest.empty()) {
		if (rest.size() < 2 || rest[0] != '-') return false;
		e.suffix = rest.substr(1);
	}

	e.name = fname.substr(0, fname.size() - 4);
	*out = e;
	return true;
}

struct Pick {
	const TilEntry* entry = nullptr; // nullptr if too old
	bool too_new = false;
};

/*
	The newest til whose version is not above v. Newer than all of them picks
	the newest one (too_new)
*/
inline Pick pick(const std::vector<TilEntry>& all, const Version& v) {
	Pick p;
	Version newest;
	bool any = false;

	for (const TilEntry& e : all) {
		if (!any || newest < e.v) newest = e.v;
		any = true;

		if (v < e.v) continue;

		if (p.entry == nullptr || p.entry->v < e.v ||
			(!(e.v < p.entry->v) && p.entry->suffix > e.suffix))
			p.entry = &e;
	}

	p.too_new = p.entry != nullptr && newest < v;
	return p;
}

} // namespace rv::til
