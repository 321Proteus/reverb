#include "host.hpp"
#include "formats/host.hpp"
#include <algorithm>
#include <cctype>
#include <cstdarg>
#include <cstdio>

static std::FILE* g_log_file = nullptr;

void rv_host_log(const char* fmt, ...) {
	char line[2048];
	std::va_list va;
	va_start(va, fmt);
	std::vsnprintf(line, sizeof(line), fmt, va);
	va_end(va);

	std::printf("[minihost] %s\n", line);
	std::fflush(stdout);
	if (g_log_file != nullptr) {
		std::fprintf(g_log_file, "[minihost] %s\n", line);
		std::fflush(g_log_file);
	}
}

namespace rv::host {

bool set_log_file(const char* path) {
	if (g_log_file != nullptr) std::fclose(g_log_file);
	g_log_file = std::fopen(path, "w");
	return g_log_file != nullptr;
}

/**
 * @brief Create a lowercase version of the input string
 */
static std::string lower(std::string s) {
	std::transform(s.begin(), s.end(), s.begin(),
		[](unsigned char c) { return (char)std::tolower(c); });
	return s;
}

std::optional<Interface> parse_interface(const std::string& name) {
	std::string n = lower(name);

	for (int i = 0; i < int(Interface::_COUNT); i++) {
		Interface iface = Interface(i);

		const HostModule* m = get_host_module(iface);
		if (m != nullptr && lower(get_interface_name(iface)) == n) return iface;
	}

	return std::nullopt;
}

} // namespace rv::host
