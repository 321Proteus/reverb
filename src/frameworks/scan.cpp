#include "common/framework.hpp"
#include "reverb.hpp"
#include "util.hpp"
#include <algorithm>
#include <bytes.hpp>
#include <map>
#include <range.hpp>
#include <segment.hpp>
#include <vector>

namespace rv {

struct signal_hit_t {
	const fw_signal_t* sig;
	ea_t ea;
};

static const std::vector<fw_signal_t> signals = {
	{Framework::DPF, "N7DISTRHO", 10},
	{Framework::DPF, "@DISTRHO@@", 10},
	{Framework::DPF, "DistrhoPluginInternal", 5},
	{Framework::IPLUG2, "N5iplug", 10},
	{Framework::IPLUG2, "@iplug@@", 10},
	{Framework::JUCE, "N4juce", 10},
	{Framework::JUCE, "@juce@@", 10},
	{Framework::JUCE, "JUCE v", 5},
};

static constexpr int FW_THRESHOLD = 10;

static bool is_ok_seg(const segment_t* s) {
	if (s->type == SEG_CODE || s->type == SEG_XTRN || s->type == SEG_BSS)
		return false;
	// Mach-O __TEXT data segs __const and __cstring inherit R|X perms, so
	// perms are only important when the segment type says nothing
	if (s->type == SEG_NORM && s->perm != 0)
		return (s->perm & SEGPERM_EXEC) == 0;
	return true;
}

static bool compile(compiled_binpat_vec_t& out, const char* lit) {
	qstring text, err;
	text.sprnt("\"%s\"", lit);

	if (!parse_binpat_str(
			&out, inf_get_min_ea(), text.c_str(), 16, PBSENC_DEF1BPU, &err)) {
		rv_log("Bad signal pattern \"%s\": %s", lit, err.c_str());

		return false;
	}

	return true;
}

static std::vector<signal_hit_t> find_fw_signals() {

	std::vector<signal_hit_t> hits;
	rangevec_t ranges;

	for (segment_t* s = get_first_seg(); s != nullptr;
		s = get_next_seg(s->start_ea)) {

		if (is_ok_seg(s)) ranges.push_back(range_t(s->start_ea, s->end_ea));
	}

	for (const fw_signal_t& fwp : signals) {
		compiled_binpat_vec_t bp;
		if (!compile(bp, fwp.pattern)) continue;

		const int flags = BIN_SEARCH_FORWARD | BIN_SEARCH_CASE |
						  BIN_SEARCH_NOBREAK | BIN_SEARCH_NOSHOW;

		for (const range_t& r : ranges) {
			ea_t ea = bin_search(r.start_ea, r.end_ea, bp, flags);
			if (ea != BADADDR) {
				rv_log("Framework pattern \"%s\" at %a", fwp.pattern, ea);
				hits.push_back({&fwp, ea});
				break;
			}
		}
	}

	return hits;
}

// SystemStats::getJUCEVersion() embeds "JUCE v<major>.<minor>.<build>"
static qstring get_juce_version(const std::vector<signal_hit_t>& hits) {
	for (const signal_hit_t& hit : hits) {
		if (!streq(hit.sig->pattern, "JUCE v")) continue;

		qstring s;
		size_t len = get_max_strlit_length(hit.ea, STRTYPE_C, ALOPT_IGNHEADS);
		if (get_strlit_contents(&s, hit.ea, len, STRTYPE_C) <= 0) continue;

		s.remove(0, strlen("JUCE v"));
		if (!s.empty() && qisdigit(s[0])) return s;
	}

	return {};
}

static qstring detect_version(
	Framework fw, const std::vector<signal_hit_t>& hits) {
	switch (fw) {
	case Framework::JUCE:
		return get_juce_version(hits);
	default:
		return {};
	}
}

FrameworkInfo scan_framework() {
	rv_log("Scanning framework");
	std::vector<signal_hit_t> hits = find_fw_signals();
	std::map<Framework, int> stat;

	for (const signal_hit_t& hit : hits) {
		stat[hit.sig->fw] += hit.sig->weight;
	}

	auto by_score = [](const auto& a, const auto& b) {
		return a.second < b.second;
	};
	auto best = std::max_element(stat.begin(), stat.end(), by_score);

	FrameworkInfo info;
	if (best == stat.end() || best->second < FW_THRESHOLD) return info;

	info.fw = best->first;
	info.score = best->second;
	info.version = detect_version(info.fw, hits);

	rv_log("Detected %s %s(score %d)", get_framework_name(info.fw),
		info.version.empty() ? "" : (info.version + " ").c_str(), info.score);

	for (const auto& [fw, s] : stat)
		if (fw != best->first && s >= FW_THRESHOLD)
			rv_log("Also matched %s (score %d)", get_framework_name(fw),
				s);

	return info;
}

} // namespace rv