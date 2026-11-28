/**
 * @file launch.cpp
 * @brief Runs reverb-minihost under the debugger: sets the process options
 * for the local or the remote target, follows the run over the channel
 * (channel.cpp), and says why when the minihost didn't start
 *
 * With a remote debugger, the minihost runs on the target and listens there
 * instead (--listen): the plugin connects to it at the debugger's host, as
 * it reaches the debug server there already.
 */
// channel.hpp first: winsock2.h must come before windows.h
#include "channel.hpp"
#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif

#include "controls.hpp"
#include "core/log.hpp"
#include "launch.hpp"
#include "settings.hpp"
#include <dbg.hpp>
#include <diskio.hpp>
#include <ida.hpp>
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

/** @brief The bundle (X.vst3, X.clap) @p path is inside, or @p path itself */
static qstring bundle_of(const qstring& path) {
	qstring outer = path;
	for (const char* inner : { "/Contents/", "\\Contents\\" }) {
		const size_t at = outer.find(inner);
		if (at != qstring::npos) outer.resize(at);
	}
	return outer;
}

/**
 * @brief Where the minihost goes on the target by default: next to the
 * plugin's bundle, or next to the module when it has none
 */
static qstring default_remote_minihost(const qstring& plugin) {
	qstring dir = dir_name(bundle_of(plugin));
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

// The process options configure_minihost last chose, to tell when they were
// edited by hand since
static struct {
	qstring app, args, sdir;
} g_set;

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

	set_process_options(
		app.c_str(), args.c_str(), nullptr, sdir.c_str(), nullptr, nullptr, -1);
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
		set_log_file(qstring());
		qstring args;
		args.sprnt("--listen %d --token %s %s \"%s\"", settings.remote_port,
			channel_token().c_str(), settings_args(settings, pick).c_str(),
			remote_plugin.c_str());
		const qstring sdir = dir_name(remote_plugin);

		if (apply_options(host, args, sdir)) {
			const qstring plugin = bundle_of(remote_plugin);
			rv_log("Debugger set to run %s %s on %s", host.c_str(),
				args.c_str(), target.c_str());
			rv_log("The target requires the reverb-minihost executable at %s "
				   "and %s at %s",
				host.c_str(), base_name(plugin).c_str(),
				dir_name(plugin).c_str());
		}
		g_set = { host, args, sdir };
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
	qstring log_path;
	if (channel_port() > 0) {
		args.sprnt("--connect 127.0.0.1:%d ", channel_port());
	} else {
		log_path = make_log_path(input);
		if (!log_path.empty()) args.sprnt("--log \"%s\" ", log_path.c_str());
	}
	set_log_file(log_path);
	args.cat_sprnt("%s \"%s\"", settings_args(settings, pick).c_str(), input);

	char sdir[QMAXPATH];
	if (!qdirname(sdir, sizeof(sdir), input)) sdir[0] = '\0';

	if (apply_options(host, args, sdir))
		rv_log("Debugger set to run %s %s", host.c_str(), args.c_str());
	g_set = { host, args, sdir };
	return true;
}

/**
 * @brief Follows a run with @p args: when they say --listen, by connecting to
 * the target
 *
 * @return false if the run can't be followed
 */
static bool start_remote_run(const qstring& args) {
	const size_t at = args.find("--listen ");
	qstring host;
	const bool remote = remote_target(&host);
	const bool remote_run = at != qstring::npos;

	if (remote_run != remote)
		rv_log("The debugger target changed since the minihost was set up: "
			   "open the minihost settings and press OK, then start again");
	if (!remote_run) { // the loopback channel may still work
		begin_run(nullptr);
		return true;
	}

	sockaddr_in addr;
	const int port = atoi(args.c_str() + at + qstrlen("--listen "));
	if (host.empty() || !net::resolve_ipv4(host.c_str(), port, &addr)) {
		rv_log("Can't resolve the debugger host \"%s\": the minihost's log "
			   "and controls are unavailable",
			host.c_str());
		return false;
	}
	rv_log("Connecting to reverb-minihost at %s:%d", host.c_str(), port);
	begin_run(&addr);
	return true;
}

// Telling the user why the minihost didn't start: IDA's own message doesn't
// say, and the cause is nearly always on the target

// The debug server a target of this database's file type runs
static const char* debug_server_name() {
	switch (inf_get_filetype()) {
	case f_PE:	  return "win64_remote.exe";
	case f_MACHO: return "mac_server";
	default:	  return "linux_server";
	}
}

static void print_start_hint() {
	qstring app, args, sdir, host;
	get_process_options(&app, &args, nullptr, &sdir, &host, nullptr, nullptr);
	if (!is_minihost(app)) return;
	qstring target;
	const bool remote = remote_target(&target);
	const bool windows = inf_get_filetype() == f_PE;

	if (remote)
		rv_log("reverb-minihost didn't start on %s. There, check that:",
			target.c_str());
	else
		rv_log("reverb-minihost didn't start. Check that:");
	rv_log("  - %s exists and is executable%s, built for the target's OS "
		   "and CPU",
		app.c_str(), windows ? "" : " (chmod +x)");

	// The plugin path is the last argument, in quotes
	size_t close = args.length();
	while (close > 0 && args[close - 1] != '"')
		close--;
	size_t open = close > 0 ? close - 1 : 0;
	while (open > 0 && args[open - 1] != '"')
		open--;
	if (close > 1 && open > 0) {
		open--;
		close--;
		const qstring plugin = bundle_of(args.substr(open + 1, close));
		rv_log("  - %s is at %s", base_name(plugin).c_str(),
			dir_name(plugin).c_str());
	}

	if (remote)
		rv_log("  - %s runs there, and IDA may reach it (%s -v logs each "
			   "request)",
			debug_server_name(), debug_server_name());

	if (app != g_set.app || args != g_set.args || sdir != g_set.sdir)
		rv_log("  - the Process options were edited since REverb set them: "
			   "paths go in Debugger > REverb minihost > Settings, and "
			   "pressing OK there sets them again");

	rv_log("The same command in a terminal%s shows the real error:",
		remote ? " there" : "");
	if (windows)
		rv_log("  cd /d \"%s\" && \"%s\" %s", sdir.c_str(), app.c_str(),
			args.c_str());
	else
		rv_log("  cd \"%s\" && \"%s\" %s", sdir.c_str(), app.c_str(),
			args.c_str());
}

// Between Start process and either the process or the failure
static bool g_start_watched = false;
static bool g_start_seen = false;
static qtimer_t g_start_timer = nullptr;

static void end_start_watch() {
	g_start_watched = false;
	if (g_start_timer != nullptr) unregister_timer(g_start_timer);
	g_start_timer = nullptr;
}

// A remote start goes on in the background after the action: it has
// failed when no process came in that time
static constexpr int kStartTicks = 30; ///< Of 500 ms
static int g_start_ticks = 0;

static int idaapi start_timer_cb(void*) {
	const bool started = g_start_seen || get_process_state() != DSTATE_NOTASK;
	if (!g_start_watched || started || ++g_start_ticks >= kStartTicks) {
		if (g_start_watched && !started && !is_debugger_busy())
			print_start_hint();
		g_start_watched = false;
		g_start_timer = nullptr; // -1 unregisters it
		return -1;
	}
	return 500;
}

// The pre event names the action, the post one (for the same action) doesn't
static void on_action(bool pre, const char* name) {
	if (pre) {
		if (name == nullptr || !streq(name, "ProcessStart")) return;

		if (get_process_state() == DSTATE_NOTASK) reconfigure_minihost();

		qstring app;
		get_process_options(
			&app, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr);
		end_start_watch();
		g_start_watched =
			is_minihost(app) && get_process_state() == DSTATE_NOTASK;
		g_start_seen = false;
	} else if (g_start_watched && g_start_timer == nullptr) {
		if (g_start_seen || get_process_state() != DSTATE_NOTASK)
			end_start_watch();
		else {
			g_start_ticks = 0;
			g_start_timer = register_timer(500, start_timer_cb, nullptr);
		}
	}
}

// Another debugger may be remote, or not: the options follow it. The event
// also comes while IDA starts a process, when they must stay as they are.
static ssize_t idaapi ui_cb(void*, int code, va_list va) {
	if (code == ui_preprocess_action || code == ui_postprocess_action) {
		const bool pre = code == ui_preprocess_action;
		on_action(pre, pre ? va_arg(va, const char*) : nullptr);
		return 0;
	}
	if (code != ui_debugger_menu_change || va_arg(va, int) == 0) return 0;
	static const debugger_t* last = nullptr;
	if (dbg == last || get_process_state() != DSTATE_NOTASK) return 0;
	last = dbg;
	reconfigure_minihost();
	return 0;
}

static ssize_t idaapi dbg_cb(void*, int code, va_list va) {
	if (code == dbg_request_error) {
		const int failed = va_arg(va, int);
		if (g_start_watched && (failed == ui_dbg_start_process ||
								   failed == ui_dbg_request_start_process)) {
			end_start_watch();
			print_start_hint();
		}
		return 0;
	}
	if (code == dbg_process_start) {
		g_start_seen = true;
		qstring app, args;
		get_process_options(
			&app, &args, nullptr, nullptr, nullptr, nullptr, nullptr);
		if (is_minihost(app)) start_remote_run(args);

	} else if (code == dbg_process_exit || code == dbg_process_detach) {
		end_run();
	}
	return 0;
}

void init() {
	if (!channel_init())
		rv_log("Couldn't listen for reverb-minihost: its log goes to a file");

	hook_to_notification_point(HT_DBG, dbg_cb);
	hook_to_notification_point(HT_UI, ui_cb);
	init_controls();
}

void term() {
	term_controls();
	end_start_watch();
	unhook_from_notification_point(HT_DBG, dbg_cb);
	unhook_from_notification_point(HT_UI, ui_cb);
	channel_term();
}

} // namespace rv::debug
