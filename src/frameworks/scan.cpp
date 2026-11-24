/**
 * @file scan.cpp
 * @brief Framework detection by signal strings.
 */
#include "core/log.hpp"
#include "db/segments.hpp"
#include "framework.hpp"
#include "frameworks/dpf/dpf.hpp"
#include "frameworks/iplug2/iplug2.hpp"
#include "frameworks/juce/juce.hpp"
#include "core/reverb.hpp"
#include <algorithm>
#include <bytes.hpp>
#include <map>
#include <segment.hpp>
#include <vector>

namespace rv {

/** @brief A signal found in the binary */
struct signal_hit_t {
	const fw_signal_t* sig;
	ea_t ea;
};

/** @brief Minimum score for a framework to count as detected */
static constexpr int FW_THRESHOLD = 10;

const FrameworkModule* get_framework_module(Framework fw) {
	switch (fw) {
	case Framework::JUCE:	return &juce::module;
	case Framework::IPLUG2: return &iplug2::module;
	case Framework::DPF:	return &dpf::module;
	default:				return nullptr;
	}
}

/** @brief Compiles the literal @p lit into a binary search pattern */
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

ea_t find_fw_string(const char* lit) {
	compiled_binpat_vec_t bp;
	if (!compile(bp, lit)) return BADADDR;

	const int flags = BIN_SEARCH_FORWARD | BIN_SEARCH_CASE |
					  BIN_SEARCH_NOBREAK | BIN_SEARCH_NOSHOW;

	for (segment_t* s = get_first_seg(); s != nullptr;
		s = get_next_seg(s->start_ea)) {

		if (!is_data_seg(s)) continue;

		ea_t ea = bin_search(s->start_ea, s->end_ea, bp, flags);
		if (ea != BADADDR) return ea;
	}

	return BADADDR;
}

/** @brief Searches every module's signals */
static std::vector<signal_hit_t> find_fw_signals() {

	std::vector<signal_hit_t> hits;

	// OTHER is last and has no module
	for (int i = 0; i < int(Framework::OTHER); i++) {
		const FrameworkModule* mod = get_framework_module(Framework(i));
		if (mod == nullptr) continue;

		for (int j = 0; j < mod->num_signals; j++) {
			const fw_signal_t& fwp = mod->signals[j];

			ea_t ea = find_fw_string(fwp.pattern);
			if (ea != BADADDR) {
				rv_log("Framework pattern \"%s\" at %a", fwp.pattern, ea);
				hits.push_back({ &fwp, ea });
			}
		}
	}

	return hits;
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
	if (best == stat.end()) {
		rv_log("No framework signals found (raw SDK or unknown framework)");
		return info;
	}

	if (best->second < FW_THRESHOLD) {
		rv_log("No framework detected, best match %s (score %d) is below %d",
			get_framework_name(best->first), best->second, FW_THRESHOLD);
		return info;
	}

	info.fw = best->first;
	info.score = best->second;

	const FrameworkModule* mod = get_framework_module(info.fw);
	if (mod != nullptr && mod->detect_version != nullptr)
		info.version = mod->detect_version();

	rv_log("Detected %s %s(score %d)", get_framework_name(info.fw),
		info.version.empty() ? "" : (info.version + " ").c_str(), info.score);

	for (const auto& [fw, s] : stat)
		if (fw != best->first && s >= FW_THRESHOLD)
			rv_log("Also matched %s (score %d)", get_framework_name(fw), s);

	return info;
}

} // namespace rv