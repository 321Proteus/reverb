// net.hpp first: winsock2.h must come before anything pulling windows.h in
#include "core/net.hpp"
#include "host.hpp"
#include "formats/host.hpp"
#include <algorithm>
#include <chrono>
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
// While listening with no one connected: the log so far, sent on connection
static bool g_keep_backlog = false;
static std::string g_backlog;
static constexpr size_t kMaxBacklog = 1 << 20;

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
	} else if (g_keep_backlog && g_backlog.size() < kMaxBacklog) {
		g_backlog += std::string("[minihost] ") + line + "\n";
		if (g_backlog.size() >= kMaxBacklog)
			g_backlog += "[minihost] (the log before IDA connected is cut "
						 "here)\n";
	}
}

namespace rv::host {

// Step mode

static std::atomic<bool> g_stepping{ false };
static std::mutex g_step_mutex;
static std::condition_variable g_step_cv;
static uint64_t g_step_released = 0; ///< Counts step_next() and mode changes
static const char* g_step_phase = nullptr; ///< Waited before; null if none
static bool g_step_on_main = false; ///< Whether the main thread waits

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
	g_step_on_main = main;

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

const char* step_waiting(bool* on_main) {
	std::lock_guard<std::mutex> lock(g_step_mutex);
	if (on_main) *on_main = g_step_phase != nullptr && g_step_on_main;
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

// Splits "ip:port", ":port" or "port"; the ip defaults to every interface
static bool split_address(
	const std::string& address, std::string* ip, int* port) {
	const size_t colon = address.rfind(':');
	*ip = colon == std::string::npos || colon == 0 ? "0.0.0.0"
												   : address.substr(0, colon);
	const char* p =
		address.c_str() + (colon == std::string::npos ? 0 : colon + 1);
	*port = std::atoi(p);
	return *port > 0 && *port < 65536;
}

// How long a blocking recv on @p s waits; 0 for ever
static void set_recv_timeout(net::sock_t s, int ms) {
#ifdef _WIN32
	const DWORD t = DWORD(ms);
#else
	timeval t = { ms / 1000, (ms % 1000) * 1000 };
#endif
	setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (const char*)&t, sizeof(t));
}

// Reads the first line of @p s into @p line, the rest into @p rest
static bool read_first_line(
	net::sock_t s, std::string* line, std::string* rest) {
	std::string buf;
	char chunk[256];
	for (size_t nl; (nl = buf.find('\n')) == std::string::npos;) {
		const int n = net::recv_some(s, chunk, sizeof(chunk));
		if (n <= 0 || buf.size() > 4096) return false;
		buf.append(chunk, size_t(n));
	}
	const size_t nl = buf.find('\n');
	*line = buf.substr(0, nl > 0 && buf[nl - 1] == '\r' ? nl - 1 : nl);
	*rest = buf.substr(nl + 1);
	return true;
}

bool listen_channel(const std::string& address, const std::string& token,
	std::function<void(const std::string&)> on_line) {

	std::string ip;
	int port = 0;
	if (!split_address(address, &ip, &port) || !net::init()) return false;

	const net::sock_t listener = net::listen_at(ip.c_str(), &port, false);
	if (listener == net::BADSOCK) return false;
	{
		std::lock_guard<std::mutex> lock(g_log_mutex);
		g_keep_backlog = true;
	}
	rv_host_log("waiting for IDA at %s:%d", ip.c_str(), port);

	std::thread([listener, token, on_line = std::move(on_line)]() {
		for (;;) {
			const net::sock_t s = accept(listener, nullptr, nullptr);
			if (s == net::BADSOCK) {
				std::this_thread::sleep_for(std::chrono::milliseconds(100));
				continue;
			}
			net::no_sigpipe(s);
			net::no_delay(s);

			// A peer that says nothing doesn't hold the channel up
			std::string hello, buf;
			set_recv_timeout(s, 5000);
			const bool said = read_first_line(s, &hello, &buf);
			set_recv_timeout(s, 0);
			if (!said || hello != "hello " + token) {
				const char bad[] = "[minihost] wrong token: not connected\n";
				net::send_all(s, bad, sizeof(bad) - 1);
				net::close_sock(s);
				rv_host_log("refused a connection with a wrong token");
				continue;
			}

			{
				std::lock_guard<std::mutex> lock(g_log_mutex);
				net::send_all(s, g_backlog.data(), g_backlog.size());
				g_backlog.clear();
				g_channel = s;
			}
			rv_host_log("IDA connected");

			net::take_lines(buf, on_line);
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
			rv_host_log("IDA disconnected: waiting for it again");
		}
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
