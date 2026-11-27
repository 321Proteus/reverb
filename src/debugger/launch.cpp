/**
 * @file launch.cpp
 * @brief Runs reverb-minihost under the debugger, and talks to it over a
 * loopback TCP channel (src/core/net.hpp): its log comes up into the Output
 * window, commands go down. When the plugin can't listen, the minihost writes
 * its log to a file that is followed instead.
 */
// net.hpp first: winsock2.h must come before windows.h
#include "core/net.hpp"
#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif

#include "launch.hpp"
#include "core/log.hpp"
#include <dbg.hpp>
#include <diskio.hpp>
#include <fpro.h>
#include <kernwin.hpp>
#include <loader.hpp>
#include <nalt.hpp>
#include <pro.h>

namespace rv::debug {

#ifdef _WIN32
static const char kHostName[] = "reverb-minihost.exe";
#else
static const char kHostName[] = "reverb-minihost";
#endif

// What the minihost can host, best first. VST2 is last: its detection goes
// through the ambiguous main export.
static const Interface kHostable[] = { Interface::VST3, Interface::CLAP,
	Interface::VST2 };

// Directory of the module this code is in (the plugin's own path)
static bool plugin_dir(qstring* out) {
	char path[QMAXPATH] = {};
#ifdef _WIN32
	HMODULE h = nullptr;
	if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
								GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
			(LPCSTR)&plugin_dir, &h) ||
		GetModuleFileNameA(h, path, sizeof(path)) == 0)
		return false;
#else
	Dl_info info;
	if (dladdr((void*)&plugin_dir, &info) == 0 || info.dli_fname == nullptr)
		return false;
	qstrncpy(path, info.dli_fname, sizeof(path));
#endif
	char dir[QMAXPATH];
	if (!qdirname(dir, sizeof(dir), path)) return false;
	*out = dir;
	return true;
}

// REVERB_MINIHOST if set, else the minihost next to the plugin
static bool find_minihost(qstring* out) {
	qstring env;
	if (qgetenv("REVERB_MINIHOST", &env) && !env.empty()) {
		*out = env;
		return qfileexist(out->c_str());
	}
	qstring dir;
	if (!plugin_dir(&dir)) return false;
	char path[QMAXPATH];
	qmakepath(path, sizeof(path), dir.c_str(), kHostName, nullptr);
	*out = path;
	return qfileexist(path);
}

static bool is_minihost(const qstring& app) {
	return qstrstr(qbasename(app.c_str()), "reverb-minihost") != nullptr;
}

/// The minihost's log file, when there is no channel; set by configure_minihost
static qstring g_log_path;

/// The channel: listening for the minihost all session, one connection at a
/// time
static net::sock_t g_listener = net::BADSOCK;
static int g_port = 0;
static net::sock_t g_client = net::BADSOCK;
static std::string g_inbuf; ///< Received, a line still incomplete

/**
 * @brief The log file of the minihost for this database: in the temp dir,
 * named after the input file so that two IDA instances don't share one
 */
static qstring make_log_path(const char* input) {
	char dir[QMAXPATH];
	if (qtmpdir(dir, sizeof(dir)) == nullptr) return qstring();
	qstring name;
	name.sprnt("reverb-minihost-%s.log", qbasename(input));
	char path[QMAXPATH];
	qmakepath(path, sizeof(path), dir, name.c_str(), nullptr);
	return path;
}

static qstring lower(const char* s) {
	qstring out(s);
	for (size_t i = 0; i < out.length(); i++)
		out[i] = qtolower(out[i]);
	return out;
}

bool configure_minihost(InterfaceSet formats) {
	const Interface* pick = nullptr;
	for (const Interface& i : kHostable)
		if (formats.has(i)) {
			pick = &i;
			break;
		}
	if (pick == nullptr) return false;

	char input[QMAXPATH] = {};
	if (get_input_file_path(input, sizeof(input)) <= 0) return false;

	qstring app;
	get_process_options(
		&app, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr);
	if (!app.empty() && app != input && !is_minihost(app)) {
		rv_log("Debugger application is %s, leaving it as it is", app.c_str());
		return false;
	}

	qstring host;
	if (!find_minihost(&host)) {
		rv_log("reverb-minihost not found next to the plugin (or set "
			   "REVERB_MINIHOST): the debugger options are unchanged");
		return false;
	}

	// The channel, or the log file when the plugin couldn't listen
	qstring args;
	g_log_path.clear();
	if (g_port > 0) {
		args.sprnt("--connect 127.0.0.1:%d ", g_port);
	} else {
		g_log_path = make_log_path(input);
		if (!g_log_path.empty())
			args.sprnt("--log \"%s\" ", g_log_path.c_str());
	}
	args.cat_sprnt(
		"--format %s \"%s\"", lower(get_interface_name(*pick)).c_str(), input);

	char sdir[QMAXPATH];
	if (!qdirname(sdir, sizeof(sdir), input)) sdir[0] = '\0';

	set_process_options(
		host.c_str(), args.c_str(), nullptr, sdir, nullptr, nullptr, -1);
	rv_log("Debugger set to run %s %s", host.c_str(), args.c_str());
	return true;
}

// Following the minihost: the debuggee's stdout goes to the terminal IDA was
// started from, if any, so the minihost sends its log over the channel, or
// writes it to a file

static qtimer_t g_timer = nullptr;
static qoff64_t g_offset = 0;
static qstring g_partial; ///< A line still being written to the file

/// Prints the lines added to the log file since the last call
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

/// Accepts the minihost's connection, then prints what it sent
static void drain_channel() {
	if (g_client == net::BADSOCK) {
		if (g_listener == net::BADSOCK) return;
		g_client = net::accept_pending(g_listener);
		if (g_client == net::BADSOCK) return;
		rv_log("reverb-minihost connected");
	}

	char buf[4096];
	int n;
	while ((n = net::recv_some(g_client, buf, sizeof(buf))) > 0)
		g_inbuf.append(buf, size_t(n));
	net::take_lines(
		g_inbuf, [](const std::string& line) { msg("%s\n", line.c_str()); });

	if (n == net::CLOSED || n == net::ERROR) {
		close_client();
		rv_log("reverb-minihost disconnected");
	}
}

static int idaapi log_timer_cb(void*) {
	drain_channel();
	drain_log();
	return 200;
}

static void stop_following() {
	if (g_timer == nullptr) return;
	unregister_timer(g_timer);
	g_timer = nullptr;
	drain_channel();
	close_client();
	drain_log();
	if (!g_partial.empty()) msg("%s\n", g_partial.c_str());
	g_partial.clear();
}

bool minihost_connected() { return g_client != net::BADSOCK; }

bool send_command(const char* command) {
	if (g_client == net::BADSOCK) return false;
	std::string line(command);
	line += '\n';
	if (net::send_all(g_client, line.data(), line.size())) return true;
	close_client();
	return false;
}

// A Debugger menu command to type a command in, until the panel has buttons

static const char kCommandAction[] = "reverb:minihost_command";
static const char kCommandMenu[] = "Debugger/";

struct command_ah_t : public action_handler_t {
	int idaapi activate(action_activation_ctx_t*) override {
		static qstring last = "info";
		qstring cmd = last;
		if (!ask_str(&cmd, HIST_CMD,
				"Minihost command (info, play, pause, blocks N, quit)"))
			return 0;
		cmd.trim2();
		if (cmd.empty()) return 0;
		last = cmd;
		if (!send_command(cmd.c_str()))
			rv_log("reverb-minihost is not connected");
		return 0;
	}

	action_state_t idaapi update(action_update_ctx_t*) override {
		return minihost_connected() ? AST_ENABLE : AST_DISABLE;
	}
};
static command_ah_t g_command_ah;

static ssize_t idaapi dbg_cb(void*, int code, va_list) {
	if (code == dbg_process_start) {
		qstring app;
		get_process_options(
			&app, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr);
		if (!is_minihost(app)) return 0;

		close_client(); // anything left from an earlier run
		if (!g_log_path.empty()) {
			// The process hasn't run yet: a log left by an earlier run goes
			qunlink(g_log_path.c_str());
			g_offset = 0;
			g_partial.clear();
		}
		if (g_timer == nullptr)
			g_timer = register_timer(200, log_timer_cb, nullptr);

	} else if (code == dbg_process_exit || code == dbg_process_detach) {
		stop_following();
	}
	return 0;
}

void init() {
	if (net::init()) g_listener = net::listen_loopback(&g_port);
	if (g_listener == net::BADSOCK) {
		g_port = 0;
		rv_log("Couldn't listen for reverb-minihost: its log goes to a file");
	}

	hook_to_notification_point(HT_DBG, dbg_cb);

	const action_desc_t desc = ACTION_DESC_LITERAL_OWNER(kCommandAction,
		"REverb minihost command...", &g_command_ah, &PLUGIN, nullptr,
		"Send a command to reverb-minihost", -1, ADF_OT_PLUGIN);
	if (register_action(desc))
		attach_action_to_menu(kCommandMenu, kCommandAction, SETMENU_APP);
}

void term() {
	detach_action_from_menu(kCommandMenu, kCommandAction);
	unregister_action(kCommandAction);

	stop_following();
	unhook_from_notification_point(HT_DBG, dbg_cb);

	close_client();
	net::close_sock(g_listener);
	g_listener = net::BADSOCK;
	g_port = 0;
	net::term();
}

} // namespace rv::debug
