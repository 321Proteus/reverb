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

	qstring args;
	args.sprnt(
		"--format %s \"%s\"", lower(get_interface_name(*pick)).c_str(), input);

	char sdir[QMAXPATH];
	if (!qdirname(sdir, sizeof(sdir), input)) sdir[0] = '\n';

	set_process_options(
		host.c_str(), args.c_str(), nullptr, sdir, nullptr, nullptr, -1);
	rv_log("Debugger set to run %s %s", host.c_str(), args.c_str());
	return true;
}

} // namespace rv::debug
