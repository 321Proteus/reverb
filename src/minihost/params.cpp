#include "params.hpp"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace rv::host {

static std::string lower(std::string s) {
	for (char& c : s)
		c = char(std::tolower((unsigned char)c));
	return s;
}

static constexpr size_t kMaxListed = 200;

static void log_param(size_t index, const Param& p) {
	rv_host_log("  [%zu] #%u %s = %s (%.4f)%s", index, p.id, p.name.c_str(),
		p.text.empty() ? "?" : p.text.c_str(), p.value,
		p.stepped ? ", stepped" : "");
}

void list_params(const PluginHost& host, const std::string& filter) {
	const size_t n = host.param_count();
	if (n == 0) {
		rv_host_log("no parameters");
		return;
	}

	const std::string f = lower(filter);
	size_t shown = 0, matched = 0;

	for (size_t i = 0; i < n; i++) {
		Param p;

		if (!host.param(i, &p)) continue;
		if (!f.empty() && lower(p.name).find(f) == std::string::npos) continue;

		if (++matched <= kMaxListed) {
			log_param(i, p);
			shown++;
		}
	}

	if (matched > shown) {
		rv_host_log("%zu more not shown: 'params <part of a name>' narrows "
					"the list",
			matched - shown);
	}

	rv_host_log(f.empty() ? "%zu parameters" : "%zu of %zu parameters match",
		f.empty() ? n : matched, n);
}

// Finds the parameter @p which names, logging why when there is none
static bool find_param(const PluginHost& host, const std::string& which,
	size_t* index, Param* out) {
	const size_t n = host.param_count();
	if (n == 0) {
		rv_host_log("no parameters");
		return false;
	}

	char* end = nullptr;
	if (which.size() > 1 && which[0] == '#') { // an id
		const unsigned long id = std::strtoul(which.c_str() + 1, &end, 0);
		for (size_t i = 0; *end == 0 && i < n; i++)
			if (host.param(i, out) && out->id == id) {
				*index = i;
				return true;
			}
		rv_host_log("no parameter has the id %s", which.c_str() + 1);
		return false;
	}

	const unsigned long i = std::strtoul(which.c_str(), &end, 10);
	if (!which.empty() && *end == 0) { // an index
		if (i < n && host.param(i, out)) {
			*index = i;
			return true;
		}

		rv_host_log("no parameter %lu: there are %zu", i, n);
		return false;
	}

	// A name: exact, else the only one holding it
	const std::string w = lower(which);
	std::vector<size_t> partial;
	for (size_t k = 0; k < n; k++) {
		Param p;
		if (!host.param(k, &p)) continue;

		const std::string name = lower(p.name);
		if (name == w) {
			*index = k;
			*out = p;
			return true;
		}

		if (name.find(w) != std::string::npos) partial.push_back(k);
	}

	if (partial.size() == 1) {
		*index = partial[0];
		return host.param(partial[0], out);
	}

	if (partial.empty()) {
		rv_host_log("no parameter is named like '%s'", which.c_str());
		return false;
	}

	rv_host_log("'%s' is in %zu parameter names; say which:", which.c_str(),
		partial.size());

	for (size_t k = 0; k < partial.size() && k < 8; k++) {
		Param p;
		if (host.param(partial[k], &p)) log_param(partial[k], p);
	}

	return false;
}

bool set_param(PluginHost& host, const std::string& which, double value) {
	if (!(value >= 0.0 && value <= 1.0)) {
		rv_host_log("a value goes from 0 to 1 (normalized)");
		return false;
	}

	size_t index = 0;
	Param p;
	if (!find_param(host, which, &index, &p)) return false;
	if (!host.set_param(p.id, value)) return false;

	rv_host_log("set [%zu] #%u %s: %.4f -> %.4f", index, p.id, p.name.c_str(),
		p.value, value);

	// A queued change shows once a block has taken it
	Param now;
	if (host.param(index, &now) && std::fabs(now.value - value) < 0.0001)
		rv_host_log("  now %s", now.text.empty() ? "?" : now.text.c_str());

	return true;
}

// FNV-1a
static uint32_t fnv1a(const std::vector<uint8_t>& data) {
	uint32_t h = 2166136261u;
	for (uint8_t b : data)
		h = (h ^ b) * 16777619u;
	return h;
}

// The size, hash and first bytes of a state blob
static void log_blob(const char* what, const std::vector<uint8_t>& data) {
	char head[16 * 3 + 1] = {};
	char ascii[17] = {};
	const size_t n = std::min<size_t>(data.size(), 16);

	for (size_t i = 0; i < n; i++) {
		std::snprintf(head + i * 3, 4, "%02x ", data[i]);
		ascii[i] = std::isprint(data[i]) ? char(data[i]) : '.';
	}

	rv_host_log("%s: %zu bytes, fnv1a %08x, starts %s|%s|", what, data.size(),
		fnv1a(data), head, ascii);
}

bool save_state_file(PluginHost& host, const std::string& path) {
	std::vector<uint8_t> data;
	if (!host.save_state(&data)) return false;

	std::FILE* f = std::fopen(path.c_str(), "wb");
	if (f == nullptr) {
		rv_host_log("can't write %s", path.c_str());
		return false;
	}

	const bool ok = data.empty() ||
					std::fwrite(data.data(), 1, data.size(), f) == data.size();
	std::fclose(f);
	if (!ok) {
		rv_host_log("writing %s failed", path.c_str());
		return false;
	}

	log_blob(("state saved to " + path).c_str(), data);

	return true;
}

bool load_state_file(PluginHost& host, const std::string& path) {
	std::FILE* f = std::fopen(path.c_str(), "rb");

	if (f == nullptr) {
		rv_host_log("can't read %s", path.c_str());
		return false;
	}

	std::vector<uint8_t> data;
	uint8_t buf[65536];
	size_t n;

	while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0)
		data.insert(data.end(), buf, buf + n);

	std::fclose(f);

	log_blob(("loading the state from " + path).c_str(), data);
	if (!host.load_state(data)) return false;

	rv_host_log("state loaded");
	return true;
}

} // namespace rv::host
