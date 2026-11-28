/**
 * @file channel.cpp
 * @brief Following reverb-minihost: the debuggee's stdout goes to the
 * terminal IDA was started from, if any, so the minihost sends its log over
 * the channel, or writes it to a file
 */
#include "channel.hpp"
#include "controls.hpp"
#include "core/log.hpp"
#include "settings.hpp"
#include <diskio.hpp>
#include <fpro.h>
#include <kernwin.hpp>

namespace rv::debug {

// The log file, when there is no channel
static qstring g_log_path;

// Listening for the minihost all session, one connection at a time
static net::sock_t g_listener = net::BADSOCK;
static int g_port = 0;
static net::sock_t g_client = net::BADSOCK;
static std::string g_inbuf; ///< Received, a line still incomplete

// A remote run: the minihost listens on the target, and the plugin connects
static bool g_remote_run = false;
static sockaddr_in g_remote_addr = {};
static net::sock_t g_pending = net::BADSOCK; ///< Connecting, not yet done
static int g_pending_ticks = 0;
static constexpr int kConnectTicks = 15; ///< Timer ticks before a retry

static qtimer_t g_timer = nullptr;
static qoff64_t g_offset = 0;
static qstring g_partial; ///< A line still being written to the file

// Prints the lines added to the log file since the last call
static void drain_log() {
	if (g_log_path.empty()) return;

	FILE* fp = qfopen(g_log_path.c_str(), "rb");
	if (fp == nullptr) return; // not created yet

	qfseek(fp, 0, SEEK_END);
	const qoff64_t size = qftell(fp);

	if (size < g_offset) { // truncated: a new run
		g_offset = 0;
		g_partial.clear();
	}

	qfseek(fp, g_offset, SEEK_SET);

	char buf[4096];
	ssize_t n;
	while ((n = qfread(fp, buf, sizeof(buf))) > 0) {
		g_offset += n;
		g_partial.append(buf, n);
	}

	qfclose(fp);

	for (size_t nl; (nl = g_partial.find('\n')) != qstring::npos;) {
		msg("%s\n", g_partial.substr(0, nl).c_str());
		g_partial.remove(0, nl + 1);
	}
}

static void close_client() {
	if (g_client == net::BADSOCK) return;

	net::close_sock(g_client);
	g_client = net::BADSOCK;
	if (!g_inbuf.empty()) msg("%s\n", g_inbuf.c_str());

	g_inbuf.clear();
}

static void close_pending() {
	net::close_sock(g_pending);
	g_pending = net::BADSOCK;
}

// Connects to the remote minihost, retrying until it listens; true once
// connected. It refuses a connection already taken, so retrying is harmless.
static bool connect_remote() {

	if (g_pending == net::BADSOCK) {
		g_pending = net::connect_start(g_remote_addr);
		g_pending_ticks = 0;
		if (g_pending == net::BADSOCK) return false;
	}

	const int state = net::connect_poll(g_pending);
	if (state == net::CONNECT_PENDING && ++g_pending_ticks < kConnectTicks)
		return false;

	if (state != net::CONNECT_DONE) { // refused: not listening yet
		close_pending();
		return false;
	}

	g_client = g_pending;
	g_pending = net::BADSOCK;
	qstring hello("hello ");
	hello += channel_token();
	hello += '\n';

	if (!net::send_all(g_client, hello.c_str(), hello.length())) {
		net::close_sock(g_client);
		g_client = net::BADSOCK;
		return false;
	}

	return true;
}

// Accepts the minihost's connection (or makes it, to a remote one), then
// prints what it sent
static void drain_channel() {
	if (g_client == net::BADSOCK) {
		if (g_remote_run) {
			if (!connect_remote()) return;

		} else {
			if (g_listener == net::BADSOCK) return;

			g_client = net::accept_pending(g_listener);
			if (g_client == net::BADSOCK) return;
		}

		rv_log("reverb-minihost connected");
		refresh_controls();
	}

	char buf[4096];
	int n;
	while ((n = net::recv_some(g_client, buf, sizeof(buf))) > 0)
		g_inbuf.append(buf, size_t(n));

	net::take_lines(
		g_inbuf, [](const std::string& line) { msg("%s\n", line.c_str()); });

	if (n == net::RECV_CLOSED || n == net::RECV_ERROR) {
		close_client();
		rv_log("reverb-minihost disconnected");
		refresh_controls();
	}
}

static int idaapi log_timer_cb(void*) {
	drain_channel();
	drain_log();
	return 200;
}

void end_run() {
	if (g_timer == nullptr) return;

	unregister_timer(g_timer);
	g_timer = nullptr;
	drain_channel();
	close_client();
	close_pending();
	g_remote_run = false;
	drain_log();

	if (!g_partial.empty()) msg("%s\n", g_partial.c_str());
	g_partial.clear();
}

void begin_run(const sockaddr_in* remote) {
	close_client(); // anything left from an earlier run
	close_pending();

	g_remote_run = remote != nullptr;
	if (remote != nullptr) g_remote_addr = *remote;

	if (!g_log_path.empty()) {
		// The process hasn't run yet (a log left by an earlier run goes)
		qunlink(g_log_path.c_str());
		g_offset = 0;
		g_partial.clear();
	}

	if (g_timer == nullptr)
		g_timer = register_timer(200, log_timer_cb, nullptr);
}

void set_log_file(const qstring& path) { g_log_path = path; }

int channel_port() { return g_port; }

bool minihost_connected() { return g_client != net::BADSOCK; }

bool send_command(const char* command) {
	if (g_client == net::BADSOCK) return false;

	std::string line(command);
	line += '\n';
	if (net::send_all(g_client, line.data(), line.size())) return true;

	close_client();
	return false;
}

bool channel_init() {
	if (net::init()) g_listener = net::listen_loopback(&g_port);
	if (g_listener != net::BADSOCK) return true;

	g_port = 0;
	return false;
}

void channel_term() {
	end_run();
	close_client();
	net::close_sock(g_listener);

	g_listener = net::BADSOCK;
	g_port = 0;
	net::term();
}

} // namespace rv::debug
