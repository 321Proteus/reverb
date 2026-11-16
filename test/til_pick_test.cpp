// Standalone test of the type library file name parsing and version pick.
//   g++ -std=c++17 -Isrc test/til_pick_test.cpp -o /tmp/til_pick_test &&
//   /tmp/til_pick_test
#include "db/til/pick.hpp"
#include <cstdio>
#include <cstdlib>

using namespace rv::til;

static int failures = 0;
#define CHECK(c)                                                               \
	do {                                                                       \
		if (!(c)) {                                                            \
			std::printf("FAIL line %d: %s\n", __LINE__, #c);                   \
			failures++;                                                        \
		}                                                                      \
	} while (0)

static std::vector<TilEntry> make(std::initializer_list<const char*> names) {
	std::vector<TilEntry> v;
	for (const char* n : names) {
		TilEntry e;
		if (parse_file_name(n, "juce", "msvc64", &e)) v.push_back(e);
	}
	return v;
}

static Version ver(const char* s) {
	Version v;
	CHECK(parse_version(s, &v));
	return v;
}

int main() {
	TilEntry e;
	CHECK(parse_file_name("rv_juce_v8.0.9_msvc64.til", "juce", "msvc64", &e));
	CHECK(e.v.major == 8 && e.v.minor == 0 && e.v.patch == 9);
	CHECK(e.suffix.empty() && e.name == "rv_juce_v8.0.9_msvc64");
	CHECK(parse_file_name(
		"rv_juce_v7.0.13-security_msvc64.til", "juce", "msvc64", &e));
	CHECK(e.suffix == "security" && e.v.patch == 13);
	CHECK(parse_file_name(
		"rv_juce_v7.0.3-vst2-only_msvc64.til", "juce", "msvc64", &e));
	CHECK(e.vst2_only());
	CHECK(!parse_file_name("rv_juce_v8.0.9_msvc32.til", "juce", "msvc64", &e));
	CHECK(!parse_file_name("rv_formats_msvc64.til", "juce", "msvc64", &e));
	CHECK(!parse_file_name("rv_juce_v8.0_msvc64.til", "juce", "msvc64", &e));
	CHECK(!parse_file_name("rv_juce_v8.0.9x_msvc64.til", "juce", "msvc64", &e));
	CHECK(!parse_file_name("rv_juce_v_msvc64.til", "juce", "msvc64", &e));

	// a source without versions
	CHECK(parse_unversioned_file_name(
		"rv_formats_msvc64.til", "formats", "msvc64", &e));
	CHECK(e.name == "rv_formats_msvc64" && e.suffix.empty());
	CHECK(!parse_unversioned_file_name(
		"rv_formats_msvc32.til", "formats", "msvc64", &e));
	CHECK(!parse_unversioned_file_name(
		"rv_juce_v8.0.9_msvc64.til", "formats", "msvc64", &e));
	CHECK(!parse_file_name("rv_formats_msvc64.til", "formats", "msvc64", &e));

	Version v;
	CHECK(parse_version("8.0.9", &v) && v.patch == 9);
	CHECK(parse_version("v6.1", &v) && v.minor == 1 && v.patch == 0);
	CHECK(!parse_version("8", &v));
	CHECK(!parse_version("", &v));

	const auto tils = make({ "rv_juce_v5.0.0_msvc64.til",
		"rv_juce_v5.4.7_msvc64.til", "rv_juce_v7.0.2_msvc64.til",
		"rv_juce_v7.0.3-vst2-only_msvc64.til", "rv_juce_v7.0.4_msvc64.til",
		"rv_juce_v7.0.13-security_msvc64.til", "rv_juce_v8.0.9_msvc64.til" });
	CHECK(tils.size() == 7);

	// the last tag not above the version
	Pick p = pick(tils, ver("8.0.9"), false);
	CHECK(p.entry && p.entry->name == "rv_juce_v8.0.9_msvc64" && !p.too_new);
	p = pick(tils, ver("8.0.8"), false);
	CHECK(p.entry && p.entry->name == "rv_juce_v7.0.13-security_msvc64");
	p = pick(tils, ver("5.4.9"), false);
	CHECK(p.entry && p.entry->name == "rv_juce_v5.4.7_msvc64" && !p.too_new);

	// newer than every tag: the newest, flagged
	p = pick(tils, ver("9.0.5"), false);
	CHECK(p.entry && p.entry->name == "rv_juce_v8.0.9_msvc64" && p.too_new);

	// older than the first: nothing
	p = pick(tils, ver("4.3.1"), false);
	CHECK(p.entry == nullptr && !p.too_new);

	// 7.0.3 has only a VST2 til: VST2 plugins get it, VST3 ones fall back
	p = pick(tils, ver("7.0.3"), true);
	CHECK(p.entry && p.entry->name == "rv_juce_v7.0.3-vst2-only_msvc64");
	CHECK(!p.partial_skipped);
	p = pick(tils, ver("7.0.3"), false);
	CHECK(p.entry && p.entry->name == "rv_juce_v7.0.2_msvc64");
	CHECK(p.partial_skipped);
	p = pick(tils, ver("7.0.4"), false);
	CHECK(p.entry && p.entry->name == "rv_juce_v7.0.4_msvc64");
	CHECK(!p.partial_skipped);

	CHECK(pick({}, ver("8.0.9"), true).entry == nullptr);

	if (failures == 0) std::printf("til_pick: all passed\n");
	return failures == 0 ? 0 : 1;
}
