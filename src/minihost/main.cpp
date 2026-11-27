#include "engine.hpp"
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
static std::atomic<Engine*> g_engine{ nullptr };

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
		"  --exit       unload once loaded, or once --blocks are run (test)\n"
		"\n"
		"Processing (no audio device: a test signal in, the output dropped):\n"
		"  --rate HZ        sample rate (default 48000)\n"
		"  --block N        frames per block (default 512)\n"
		"  --signal S       sine | impulse | noise | silence (default sine)\n"
		"  --no-notes       no note every second for instruments\n"
		"  --blocks N       run N blocks, then pause (default: keep playing)\n"
		"  --paused         activate, but don't run blocks until 'play'\n"
		"  --fast           don't pace the blocks in real time\n"
		"  --no-process     stop after creating the instance\n"
		"  --connect A  connect to the REverb plugin at A (127.0.0.1:port):\n"
		"               the log goes to IDA, and IDA can send commands\n"
		"  --log FILE   also write the log to FILE (the REverb plugin shows "
		"it in IDA's Output window)\n"
		"  -h, --help\n"
		"\n"
		"While running: 'info', 'play', 'pause', 'blocks N', 'quit' (or "
		"Ctrl-C).\n",
		argv0);
}

/**
 * @brief Runs one command, from stdin or the REverb plugin's channel. Replies
 * go through the log, so they reach IDA too.
 */
static void handle_command(const std::string& line) {
	Engine* e = g_engine;

	if (line == "quit" || line == "q" || line == "exit") g_quit = true;

	else if (line == "info" || line == "i")
		g_info = true;

	else if (e && line == "play") {
		e->play();
		rv_host_log("playing");

	} else if (e && line == "pause") {
		e->pause();
		rv_host_log("paused");

	} else if (e && line.rfind("blocks ", 0) == 0) {
		const long long n = std::atoll(line.c_str() + 7);
		e->run_blocks(n);
		rv_host_log("running %lld blocks", n);

	} else if (!line.empty())
		rv_host_log("unknown command '%s'; commands: info, quit%s",
			line.c_str(), e ? ", play, pause, blocks N" : "");
}

static void command_thread() {
	std::string line;
	while (std::getline(std::cin, line))
		handle_command(line);
}

int main(int argc, char** argv) {
	std::string path;
	std::optional<Interface> format;
	LoadOptions opts;
	std::string connect_to;
	EngineOptions eng;

	bool once = false;
	bool processing = true;
	bool paused = false;
	int64_t blocks = -1; // -1: play

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

		} else if (a == "--connect" && i + 1 < argc) {
			connect_to = argv[++i];

		} else if (a == "--log" && i + 1 < argc) {
			const char* log = argv[++i];
			if (!set_log_file(log))
				std::fprintf(stderr, "can't open the log file %s\n", log);

		} else if (a == "--class" && i + 1 < argc)
			opts.class_index = std::atoi(argv[++i]);

		else if (a == "--rate" && i + 1 < argc)
			eng.setup.sample_rate = std::atof(argv[++i]);

		else if (a == "--block" && i + 1 < argc)
			eng.setup.block_size = uint32_t(std::atoi(argv[++i]));

		else if (a == "--signal" && i + 1 < argc) {
			auto sig = parse_signal(argv[++i]);
			if (!sig) {
				std::fprintf(stderr, "unknown signal\n");
				return 2;
			}
			eng.signal = *sig;

		} else if (a == "--blocks" && i + 1 < argc)
			blocks = std::atoll(argv[++i]);

		else if (a == "--no-notes")
			eng.notes = false;

		else if (a == "--paused")
			paused = true;

		else if (a == "--fast")
			eng.realtime = false;

		else if (a == "--no-process")
			processing = false;

		else if (!a.empty() && a[0] != '-' && path.empty())
			path = a;

		else {
			usage(argv[0]);
			return 2;
		}
	}

	if (path.empty() || eng.setup.sample_rate < 1000 ||
		eng.setup.block_size == 0 || eng.setup.block_size > 65536) {
		usage(argv[0]);
		return 2;
	}
	mark_main_thread();

	// First, so that IDA also gets the log of loading
	if (!connect_to.empty() && !connect_channel(connect_to, handle_command))
		rv_host_log(
			"can't connect to the REverb plugin at %s", connect_to.c_str());

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

	// --exit without --blocks is just load and unload
	std::unique_ptr<Engine> engine;

	if (processing && !(once && blocks < 0)) {
		engine = std::make_unique<Engine>(*host, eng);
		if (!engine->start()) engine.reset();
	}

	if (engine) {
		if (paused) engine->pause();
		else if (blocks >= 0)
			engine->run_blocks(blocks);
		else
			engine->play();
		g_engine = engine.get();
	}

	if (once && engine) {
		// Until the blocks are run, still servicing the main thread
		while (engine->busy() && !g_quit) {
			host->idle();
			pump_main_thread(10);
		}
		engine->describe();

	} else if (!once) {
		std::signal(SIGINT, on_signal);
		std::signal(SIGTERM, on_signal);
		std::thread(command_thread).detach();

		rv_host_log(
			"plugin is alive, attach a debugger to pid %d ('info', 'quit')",
			(int)getpid());

		while (!g_quit) {
			host->idle();
			if (g_info.exchange(false)) {
				host->describe();
				if (engine) engine->describe();
			}
			pump_main_thread(50);
		}
	}

	g_engine = nullptr;
	if (engine) engine->stop();
	engine.reset();

	rv_host_log("unloading");
	host->unload();
	host.reset();

	return 0;
}
