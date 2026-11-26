/**
 * @file launch.cpp
 * @todo Replace the log file with a loopback TCP channel
 */
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
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

/// The minihost's log file, set by configure_minihost
static qstring g_log_path;

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

	g_log_path = make_log_path(input);

	qstring args;
	if (!g_log_path.empty()) args.sprnt("--log \"%s\" ", g_log_path.c_str());
	args.cat_sprnt(
		"--format %s \"%s\"", lower(get_interface_name(*pick)).c_str(), input);

	char sdir[QMAXPATH];
	if (!qdirname(sdir, sizeof(sdir), input)) sdir[0] = '\0';

	set_process_options(
		host.c_str(), args.c_str(), nullptr, sdir, nullptr, nullptr, -1);
	rv_log("Debugger set to run %s %s", host.c_str(), args.c_str());
	return true;
}

// Following the minihost's log: the debuggee's stdout goes to the terminal
// IDA was started from, if any, so the minihost also writes a file

static qtimer_t g_timer = nullptr;
static qoff64_t g_offset = 0;
static qstring g_partial; ///< A line still being written

/// Prints the lines added to the log since the last call
static void drain_log() {
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

static int idaapi log_timer_cb(void*) {
	drain_log();
	return 200;
}

static void stop_following() {
	if (g_timer == nullptr) return;
	unregister_timer(g_timer);
	g_timer = nullptr;
	drain_log();
	if (!g_partial.empty()) msg("%s\n", g_partial.c_str());
	g_partial.clear();
}

static ssize_t idaapi dbg_cb(void*, int code, va_list) {
	if (code == dbg_process_start) {
		qstring app;
		get_process_options(
			&app, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr);
		if (g_log_path.empty() || !is_minihost(app)) return 0;

		// The process hasn't run yet: a log left by an earlier run goes
		qunlink(g_log_path.c_str());
		g_offset = 0;
		g_partial.clear();
		if (g_timer == nullptr)
			g_timer = register_timer(200, log_timer_cb, nullptr);
	} else if (code == dbg_process_exit || code == dbg_process_detach) {
		stop_following();
	}
	return 0;
}

void init() { hook_to_notification_point(HT_DBG, dbg_cb); }

void term() {
	stop_following();
	unhook_from_notification_point(HT_DBG, dbg_cb);
}

} // namespace rv::debug
