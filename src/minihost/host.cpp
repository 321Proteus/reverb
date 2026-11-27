// net.hpp first: winsock2.h must come before anything pulling windows.h in
#include "core/net.hpp"
#include "host.hpp"
#include "formats/host.hpp"
#include <algorithm>
#include <atomic>
#include <condition_variable>
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

// Step mode

static std::atomic<bool> g_stepping{ false };
static std::mutex g_step_mutex;
static std::condition_variable g_step_cv;
static uint64_t g_step_released = 0; ///< Counts step_next() and mode changes
static const char* g_step_phase = nullptr; ///< Waited before; null if none

void set_step_mode(bool on) {
	{
		std::lock_guard<std::mutex> lock(g_step_mutex);
		g_stepping = on;
		g_step_released++;
	}
	g_step_cv.notify_all();
}

void step(const char* phase) {
	if (!g_stepping) return;
	rv_host_log(
		"step: before %s ('next' to go on, 'run' to stop stepping)", phase);

	std::unique_lock<std::mutex> lock(g_step_mutex);
	const uint64_t seen = g_step_released;
	g_step_phase = phase;
	const bool main = on_main_thread();

	while (g_step_released == seen) {
		if (!main) {
			g_step_cv.wait(lock);
			continue;
		}
		// The main thread still services its run loop while it waits
		lock.unlock();
		pump_main_thread(10);
		lock.lock();
	}
	g_step_phase = nullptr;
}

const char* step_waiting() {
	std::lock_guard<std::mutex> lock(g_step_mutex);
	return g_step_phase;
}

bool step_next() {
	{
		std::lock_guard<std::mutex> lock(g_step_mutex);
		if (g_step_phase == nullptr) return false;
		g_step_released++;
	}
	g_step_cv.notify_all();
	return true;
}

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
