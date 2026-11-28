/**
 * @file launch.cpp
 * @brief Runs reverb-minihost under the debugger, and talks to it over a
 * loopback TCP channel (src/core/net.hpp): its log comes up into the Output
 * window, commands go down. When the plugin can't listen, the minihost writes
 * its log to a file that is followed instead.
 *
 * With a remote debugger, the minihost runs on the target and listens there
 * instead (--listen): the plugin connects to it at the debugger's host, as
 * it reaches the debug server there already.
 */
// net.hpp first: winsock2.h must come before windows.h
#include "core/net.hpp"
#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif

#include "controls.hpp"
#include "core/log.hpp"
#include "launch.hpp"
#include <dbg.hpp>
#include <diskio.hpp>
#include <fpro.h>
#include <ida.hpp>
#include <kernwin.hpp>
#include <loader.hpp>
#include <nalt.hpp>
#include <netnode.hpp>
#include <pro.h>
#include <random>

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

// Paths of the target, which may use either separator whatever IDA runs on
static size_t last_sep(const qstring& path) {
	const size_t slash = path.rfind('/'), back = path.rfind('\\');
	if (slash == qstring::npos) return back;
	if (back == qstring::npos) return slash;
	return qmax(slash, back);
}

static qstring base_name(const qstring& path) {
	const size_t sep = last_sep(path);
	return sep == qstring::npos ? path : path.substr(sep + 1);
}

static qstring dir_name(const qstring& path) {
	const size_t sep = last_sep(path);
	return sep == qstring::npos ? qstring() : path.substr(0, sep);
}

static bool is_minihost(const qstring& app) {
	return base_name(app).find("reverb-minihost") != qstring::npos;
}

/**
 * @brief Where the minihost goes on the target by default: next to the
 * plugin's bundle, or next to the module when it has none
 */
static qstring default_remote_minihost(const qstring& plugin) {
	qstring outer = plugin;
	for (const char* inner : { "/Contents/", "\\Contents\\" }) {
		const size_t at = outer.find(inner);
		if (at != qstring::npos) outer.resize(at); // the bundle
	}
	qstring dir = dir_name(outer);
	const char sep =
		last_sep(plugin) != qstring::npos && plugin[last_sep(plugin)] == '\\'
			? '\\'
			: '/';
	qstring out = dir;
	if (!out.empty()) out += sep;
	out += "reverb-minihost";
	if (inf_get_filetype() == f_PE) out += ".exe";
	return out;
}

// The minihost's log file, when there is no channel; set by configure_minihost
static qstring g_log_path;

// The channel: listening for the minihost all session, one connection at a
// time
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

// Settings, in the database

static const char kSettingsNode[] = "$ reverb minihost";
enum : nodeidx_t { // altvals; supstrs share the indices
	SET_SAVED,	   // 1 once saved
	SET_FORMAT,	   // the Interface + 1, 0 for the best detected
	SET_CLASS,
	SET_RATE,
	SET_BLOCK,
	SET_FLAGS,			 // FLAG_*
	SET_SIGNAL,			 // supstr
	SET_EXTRA,			 // supstr
	SET_REMOTE_MINIHOST, // supstr
	SET_REMOTE_PLUGIN,	 // supstr
	SET_REMOTE_PORT,
	SET_TOKEN, // supstr, made once per database
};

enum : nodeidx_t {
	FLAG_NOTES = 1,
	FLAG_PAUSED = 2,
	FLAG_FAST = 4,
	FLAG_NO_PROCESS = 8,
	FLAG_STEP = 16
};

host_settings_t load_settings() {
	host_settings_t s;
	netnode n(kSettingsNode);
	if (n == BADNODE || n.altval(SET_SAVED) != 1) return s;

	s.format = int(n.altval(SET_FORMAT)) - 1;
	s.class_index = int(n.altval(SET_CLASS));
	s.sample_rate = int(n.altval(SET_RATE));
	s.block_size = int(n.altval(SET_BLOCK));
	const nodeidx_t f = n.altval(SET_FLAGS);

	s.notes = (f & FLAG_NOTES) != 0;
	s.paused = (f & FLAG_PAUSED) != 0;
	s.realtime = (f & FLAG_FAST) == 0;
	s.process = (f & FLAG_NO_PROCESS) == 0;
	s.step = (f & FLAG_STEP) != 0;
	n.supstr(&s.signal, SET_SIGNAL);
	n.supstr(&s.extra, SET_EXTRA);
	n.supstr(&s.remote_minihost, SET_REMOTE_MINIHOST);
	n.supstr(&s.remote_plugin, SET_REMOTE_PLUGIN);
	if (const nodeidx_t port = n.altval(SET_REMOTE_PORT)) // 0 before it existed
		s.remote_port = int(port);
	return s;
}

void save_settings(const host_settings_t& s) {
	netnode n(kSettingsNode, 0, true);
	n.altset(SET_FORMAT, nodeidx_t(s.format + 1));
	n.altset(SET_CLASS, nodeidx_t(s.class_index));
	n.altset(SET_RATE, nodeidx_t(s.sample_rate));
	n.altset(SET_BLOCK, nodeidx_t(s.block_size));
	n.altset(SET_FLAGS,
		(s.notes ? FLAG_NOTES : 0) | (s.paused ? FLAG_PAUSED : 0) |
			(s.realtime ? 0 : FLAG_FAST) | (s.process ? 0 : FLAG_NO_PROCESS) |
			(s.step ? FLAG_STEP : 0));
	n.supset(SET_SIGNAL, s.signal.c_str());
	n.supset(SET_EXTRA, s.extra.c_str());
	n.supset(SET_REMOTE_MINIHOST, s.remote_minihost.c_str());
	n.supset(SET_REMOTE_PLUGIN, s.remote_plugin.c_str());
	n.altset(SET_REMOTE_PORT, nodeidx_t(s.remote_port));
	n.altset(SET_SAVED, 1);
}

/**
 * @brief What the plugin says first when it connects to a remote minihost,
 * so that no one else on the target's network drives it. Made once and kept
 * in the database, as the process options that carry it are.
 */
static qstring channel_token() {
	netnode n(kSettingsNode, 0, true);
	qstring token;
	if (n.supstr(&token, SET_TOKEN) > 0 && !token.empty()) return token;

	std::random_device rd;
	for (int i = 0; i < 4; i++)
		token.cat_sprnt("%08x", unsigned(rd()));
	n.supset(SET_TOKEN, token.c_str());
	return token;
}

/**
 * @brief True if the selected debugger runs the process on another machine:
 * a remote one whose host isn't this one (e.g. mac_server run here as root
 * is reached over loopback like a local debugger)
 */
static bool remote_target(qstring* host) {
	if (dbg == nullptr || !dbg->is_remote()) return false;
	get_process_options(
		nullptr, nullptr, nullptr, nullptr, host, nullptr, nullptr);
	if (host->empty()) return false;
	sockaddr_in a;
	return !net::resolve_ipv4(host->c_str(), 0, &a) || !net::is_loopback(a);
}

// What configure_minihost last saw
static InterfaceSet g_formats;

std::vector<Interface> hostable_formats() {
	std::vector<Interface> out;
	for (Interface i : kHostable)
		if (g_formats.has(i)) out.push_back(i);
	return out;
}

// The minihost's arguments for the settings, but the channel and the input
static qstring settings_args(const host_settings_t& s, Interface format) {
	const host_settings_t def;
	qstring a;
	a.sprnt("--format %s", lower(get_interface_name(format)).c_str());
	if (s.class_index != def.class_index)
		a.cat_sprnt(" --class %d", s.class_index);
	if (s.sample_rate != def.sample_rate)
		a.cat_sprnt(" --rate %d", s.sample_rate);
	if (s.block_size != def.block_size)
		a.cat_sprnt(" --block %d", s.block_size);
	if (s.signal != def.signal) a.cat_sprnt(" --signal %s", s.signal.c_str());
	if (!s.notes) a += " --no-notes";
	if (s.paused) a += " --paused";
	if (s.step) a += " --step";
	if (!s.realtime) a += " --fast";
	if (!s.process) a += " --no-process";
	if (!s.extra.empty()) a.cat_sprnt(" %s", s.extra.c_str());

	return a;
}

bool reconfigure_minihost() { return configure_minihost(g_formats); }

/**
 * @brief Sets the process options to run @p app, unless they already do:
 * setting them while IDA starts the process makes the start fail
 *
 * @return false if they were already set so
 */
static bool apply_options(
	const qstring& app, const qstring& args, const qstring& sdir) {
	qstring cur_app, cur_args, cur_sdir;
	get_process_options(
		&cur_app, &cur_args, nullptr, &cur_sdir, nullptr, nullptr, nullptr);
	if (cur_app == app && cur_args == args && cur_sdir == sdir) return false;

	set_process_options(app.c_str(), args.c_str(), nullptr, sdir.c_str(),
		nullptr, nullptr, -1);
	return true;
}

bool configure_minihost(InterfaceSet formats) {
	g_formats = formats;
	const std::vector<Interface> hostable = hostable_formats();
	if (hostable.empty()) return false;

	// The format chosen in the settings, if detected, else the best one
	const host_settings_t settings = load_settings();
	Interface pick = hostable.front();
	for (Interface i : hostable)
		if (int(i) == settings.format) pick = i;

	char input[QMAXPATH] = {};
	if (get_input_file_path(input, sizeof(input)) <= 0) return false;

	qstring app;
	get_process_options(
		&app, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr);
	const qstring remote_plugin = settings.remote_plugin.empty()
									  ? qstring(input)
									  : settings.remote_plugin;
	if (!app.empty() && app != input && app != remote_plugin &&
		!is_minihost(app)) {
		rv_log("Debugger application is %s, leaving it as it is", app.c_str());
		return false;
	}

	qstring target;
	if (remote_target(&target)) {
		// Everything is the target's: the paths, and the channel's end
		const qstring host = settings.remote_minihost.empty()
								 ? default_remote_minihost(remote_plugin)
								 : settings.remote_minihost;
		g_log_path.clear();
		qstring args;
		args.sprnt("--listen %d --token %s %s \"%s\"", settings.remote_port,
			channel_token().c_str(), settings_args(settings, pick).c_str(),
			remote_plugin.c_str());
		const qstring sdir = dir_name(remote_plugin);

		if (apply_options(host, args, sdir))
			rv_log("Debugger set to run %s %s on %s", host.c_str(),
				args.c_str(), target.c_str());
		return true;
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
	args.cat_sprnt("%s \"%s\"", settings_args(settings, pick).c_str(), input);

	char sdir[QMAXPATH];
	if (!qdirname(sdir, sizeof(sdir), input)) sdir[0] = '\0';

	if (apply_options(host, args, sdir))
		rv_log("Debugger set to run %s %s", host.c_str(), args.c_str());
	return true;
}

// Following the minihost: the debuggee's stdout goes to the terminal IDA was
// started from, if any, so the minihost sends its log over the channel, or
// writes it to a file

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

static void stop_following() {
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

bool minihost_connected() { return g_client != net::BADSOCK; }

bool send_command(const char* command) {
	if (g_client == net::BADSOCK) return false;
	std::string line(command);
	line += '\n';
	if (net::send_all(g_client, line.data(), line.size())) return true;
	close_client();
	return false;
}

/**
 * @brief Readies the channel's end for a run with @p args: when they say
 * --listen, the address of the target to connect to
 *
 * @return false if the run can't be followed
 */
static bool start_remote_run(const qstring& args) {
	const size_t at = args.find("--listen ");
	qstring host;
	const bool remote = remote_target(&host);
	g_remote_run = at != qstring::npos;

	if (g_remote_run != remote) {
		rv_log("The debugger target changed since the minihost was set up: "
			   "open the minihost settings and press OK, then start again");
		if (!g_remote_run) return true; // the loopback channel may still work
	}
	if (!g_remote_run) return true;

	const int port = atoi(args.c_str() + at + qstrlen("--listen "));
	if (host.empty() ||
		!net::resolve_ipv4(host.c_str(), port, &g_remote_addr)) {
		rv_log("Can't resolve the debugger host \"%s\": the minihost's log "
			   "and controls are unavailable",
			host.c_str());
		g_remote_run = false;
		return false;
	}
	rv_log("Connecting to reverb-minihost at %s:%d", host.c_str(), port);
	return true;
}

// Another debugger may be remote, or not: the options follow it. The event
// also comes while IDA starts a process, when they must stay as they are.
static ssize_t idaapi ui_cb(void*, int code, va_list va) {
	if (code != ui_debugger_menu_change || va_arg(va, int) == 0) return 0;
	static const debugger_t* last = nullptr;
	if (dbg == last || get_process_state() != DSTATE_NOTASK) return 0;
	last = dbg;
	reconfigure_minihost();
	return 0;
}

static ssize_t idaapi dbg_cb(void*, int code, va_list) {
	if (code == dbg_process_start) {
		qstring app, args;
		get_process_options(
			&app, &args, nullptr, nullptr, nullptr, nullptr, nullptr);
		if (!is_minihost(app)) return 0;

		close_client(); // anything left from an earlier run
		close_pending();
		if (!start_remote_run(args)) return 0;
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
	hook_to_notification_point(HT_UI, ui_cb);
	init_controls();
}

void term() {
	term_controls();
	stop_following();
	unhook_from_notification_point(HT_DBG, dbg_cb);
	unhook_from_notification_point(HT_UI, ui_cb);

	close_client();
	net::close_sock(g_listener);
	g_listener = net::BADSOCK;
	g_port = 0;
	net::term();
}

} // namespace rv::debug
