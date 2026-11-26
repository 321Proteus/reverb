#include "formats/host.hpp"
#include "host.hpp"
#include <atomic>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <thread>

#ifdef _WIN32
#include <process.h>
#define getpid _getpid
#else
#include <unistd.h>
#endif

using namespace rv;
using namespace rv::host;

static std::atomic<bool> g_quit{ false };
static std::atomic<bool> g_info{ false };

static void on_signal(int) { g_quit = true; }

// The format comes from the caller: the REverb plugin passes what it detected
// in the database (see src/debug/launch.cpp)
static void usage(const char* argv0) {
	std::printf(
		"usage: %s --format F [options] <plugin>\n"
		"  <plugin>     the plugin bundle, or the module itself\n"
		"               (the bundle is found from a binary inside one)\n"
		"  --format F   vst2 | vst3 | clap\n"
		"  --class N    which plugin of a multi-plugin module (default 0)\n"
		"  --exit       load, then unload immediately (test)\n"
		"  --log FILE   also write the log to FILE (the REverb plugin shows "
		"it in IDA's Output window)\n"
		"  -h, --help\n"
		"\n"
		"While running: 'info' prints the plugin state, 'quit' (or Ctrl-C) "
		"unloads it.\n",
		argv0);
}

// Commands come from a background thread so the main thread can keep servicing
// the plugin. EOF just stops reading: the host may be started with no stdin.
static void command_thread() {
	std::string line;
	while (std::getline(std::cin, line)) {
		if (line == "quit" || line == "q" || line == "exit") g_quit = true;

		else if (line == "info" || line == "i")
			g_info = true;

		else if (!line.empty())
			std::printf("commands: info, quit\n");
	}
}

int main(int argc, char** argv) {
	std::string path;
	std::optional<Interface> format;
	LoadOptions opts;
	bool once = false;

	for (int i = 1; i < argc; ++i) {
		std::string a = argv[i];

		if (a == "-h" || a == "--help") {
			usage(argv[0]);
			return 0;

		} else if (a == "--exit")
			once = true;

		else if (a == "--format" && i + 1 < argc) {
			format = parse_interface(argv[++i]);
			if (!format) {
				std::fprintf(stderr, "unknown format\n");
				return 2;
			}

		} else if (a == "--log" && i + 1 < argc) {
			const char* log = argv[++i];
			if (!set_log_file(log))
				std::fprintf(stderr, "can't open the log file %s\n", log);

		} else if (a == "--class" && i + 1 < argc)
			opts.class_index = std::atoi(argv[++i]);

		else if (!a.empty() && a[0] != '-' && path.empty())
			path = a;

		else {
			usage(argv[0]);
			return 2;
		}
	}

	if (path.empty()) {
		usage(argv[0]);
		return 2;
	}

	const HostModule* module = format ? get_host_module(*format) : nullptr;
	if (module == nullptr) {
		usage(argv[0]);
		return 2;
	}

	opts.bundle_path = bundle_root(path);
	opts.binary_path = resolve_binary(path);

	rv_host_log("pid %d, %s %s", (int)getpid(), get_interface_name(*format),
		opts.binary_path.c_str());

	if (opts.bundle_path != opts.binary_path)
		rv_host_log("bundle: %s", opts.bundle_path.c_str());

	SharedLib lib;
	std::string err;
	if (!lib.open(opts.binary_path, err)) {
		rv_host_log("failed to load module: %s", err.c_str());
		return 1;
	}

	auto host = module->create();
	if (!host->load(lib, opts)) {
		rv_host_log("load failed");
		host->unload();
		return 1;
	}

	host->describe();

	if (!once) {
		std::signal(SIGINT, on_signal);
		std::signal(SIGTERM, on_signal);
		std::thread(command_thread).detach();

		rv_host_log(
			"plugin is alive, attach a debugger to pid %d ('info', 'quit')",
			(int)getpid());

		while (!g_quit) {
			host->idle();
			if (g_info.exchange(false)) host->describe();
			pump_main_thread(50);
		}
	}

	rv_host_log("unloading");
	host->unload();
	host.reset();

	return 0;
}
