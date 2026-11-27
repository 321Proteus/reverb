// net.hpp first: winsock2.h must come before anything pulling windows.h in
#include "core/net.hpp"
#include "host.hpp"
#include "formats/host.hpp"
#include <algorithm>
#include <cctype>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <thread>

static std::FILE* g_log_file = nullptr;
static rv::net::sock_t g_channel = rv::net::BADSOCK;
// One line at a time from the main, audio and channel threads
static std::mutex g_log_mutex;

void rv_host_log(const char* fmt, ...) {
	char line[2048];
	std::va_list va;
	va_start(va, fmt);
	std::vsnprintf(line, sizeof(line), fmt, va);
	va_end(va);

	std::lock_guard<std::mutex> lock(g_log_mutex);
	std::printf("[minihost] %s\n", line);
	std::fflush(stdout);
	if (g_log_file != nullptr) {
		std::fprintf(g_log_file, "[minihost] %s\n", line);
		std::fflush(g_log_file);
	}
	if (g_channel != rv::net::BADSOCK) {
		std::string out = std::string("[minihost] ") + line + "\n";
		rv::net::send_all(g_channel, out.data(), out.size());
	}
}

namespace rv::host {

bool connect_channel(const std::string& address,
	std::function<void(const std::string&)> on_line) {

	const size_t colon = address.rfind(':');
	if (colon == std::string::npos || !net::init()) return false;

	const std::string ip = address.substr(0, colon);
	const int port = std::atoi(address.c_str() + colon + 1);

	const net::sock_t s = net::connect_to(ip.c_str(), port);
	if (s == net::BADSOCK) return false;
	{
		std::lock_guard<std::mutex> lock(g_log_mutex);
		g_channel = s;
	}

	// Commands come in on their own thread like the stdin ones
	std::thread([s, on_line = std::move(on_line)]() {
		std::string buf;
		char chunk[1024];
		int n;
		while ((n = net::recv_some(s, chunk, sizeof(chunk))) > 0) {
			buf.append(chunk, size_t(n));
			net::take_lines(buf, on_line);
		}

		{
			std::lock_guard<std::mutex> lock(g_log_mutex);
			g_channel = net::BADSOCK;
		}

		net::close_sock(s);
		rv_host_log("the REverb plugin closed the channel");
	}).detach();

	return true;
}

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
