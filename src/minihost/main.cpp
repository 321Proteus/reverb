#include "engine.hpp"
#include "formats/host.hpp"
#include "host.hpp"
#include "params.hpp"
#include <atomic>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

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

// Commands that call into the plugin run on the main thread: queued by the
// command threads, run by the main loop
static std::mutex g_tasks_mutex;
static std::vector<std::function<void(PluginHost&)>> g_tasks;

static void post_task(std::function<void(PluginHost&)> task) {
	bool on_main = false;
	const char* phase = step_waiting(&on_main);
	if (on_main)
		rv_host_log("queued: runs once the step before %s goes on", phase);
	std::lock_guard<std::mutex> lock(g_tasks_mutex);
	g_tasks.push_back(std::move(task));
}

static void run_tasks(PluginHost& host) {
	std::vector<std::function<void(PluginHost&)>> tasks;
	{
		std::lock_guard<std::mutex> lock(g_tasks_mutex);
		tasks.swap(g_tasks);
	}
	for (auto& t : tasks)
		t(host);
}

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
		"  --step           wait before each lifecycle phase for 'next': "
		"load,\n"
		"                   entry, create, init, (controller, state,) "
		"activate,\n"
		"                   start, process, stop, deactivate, unload\n"
		"                   ('run' stops stepping)\n"
		"  --state FILE     load the plugin's state from FILE after init\n"
		"  --connect A  connect to the REverb plugin at A (127.0.0.1:port):\n"
		"               the log goes to IDA, and IDA can send commands\n"
		"  --listen A   for a remote target: wait for the REverb plugin to\n"
		"               connect at A ([ip:]port, every interface by default)\n"
		"  --token T    what the plugin must say first when it connects\n"
		"  --log FILE   also write the log to FILE (the REverb plugin shows "
		"it in IDA's Output window)\n"
		"  -h, --help\n"
		"\n"
		"While running: 'info', 'play', 'pause', 'blocks N', 'quit' (or "
		"Ctrl-C),\n"
		"  'params [part of a name]', 'set <index|#id|name> <0..1>',\n"
		"  'save FILE' and 'load FILE' (the state blob).\n",
		argv0);
}

/**
 * @brief Runs one command, from stdin or the REverb plugin's channel. Replies
 * go through the log, so they reach IDA too.
 */
static void handle_command(const std::string& line) {
	Engine* e = g_engine;

	if (line == "quit" || line == "q" || line == "exit") {
		set_step_mode(false); // nothing waits for a step while quitting
		g_quit = true;

	} else if (line == "next" || line == "n") {
		if (!step_next()) rv_host_log("not waiting at a step");

	} else if (line == "run") {
		set_step_mode(false);
		rv_host_log("step mode off");
	}

	else if (line == "info" || line == "i") {
		// While a step waits, the main thread may be the one waiting: answer
		// here, without calling into the plugin
		if (const char* phase = step_waiting())
			rv_host_log("waiting before %s ('next' to go on)", phase);
		else
			g_info = true;
	}

	else if (line == "play") {
		// Play means go: it also ends step mode
		set_step_mode(false);
		if (e) e->play();
		rv_host_log(e ? "playing" : "step mode off");

	} else if (e && line == "pause") {
		e->pause();
		rv_host_log("paused");

	} else if (line == "params" || line.rfind("params ", 0) == 0) {
		const std::string filter = line.size() > 7 ? line.substr(7) : "";
		post_task([filter](PluginHost& h) { list_params(h, filter); });

	} else if (line.rfind("set ", 0) == 0) {
		// The value is the last word: a name may hold spaces
		const size_t sp = line.find_last_of(' ');
		const std::string which = line.substr(4, sp > 4 ? sp - 4 : 0);
		char* end = nullptr;
		const double v = std::strtod(line.c_str() + sp + 1, &end);
		if (which.empty() || end == line.c_str() + sp + 1 || *end != 0)
			rv_host_log("usage: set <index|#id|name> <0..1>");
		else
			post_task([which, v](PluginHost& h) { set_param(h, which, v); });

	} else if (line.rfind("save ", 0) == 0 || line.rfind("load ", 0) == 0) {
		const bool save = line[0] == 's';
		const std::string path = line.substr(5);
		post_task([save, path](PluginHost& h) {
			if (save) save_state_file(h, path);
			else
				load_state_file(h, path);
		});

	} else if (e && line.rfind("blocks ", 0) == 0) {
		const long long n = std::atoll(line.c_str() + 7);
		e->run_blocks(n);
		rv_host_log("running %lld blocks", n);

	} else if (!line.empty())
		rv_host_log("unknown command '%s'; commands: info, next, run, play, "
					"params, set, save, load, quit%s",
			line.c_str(), e ? ", pause, blocks N" : "");
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
	std::string connect_to, listen_at, token, state_file;
	EngineOptions eng;

	bool once = false;
	bool processing = true;
	bool paused = false;
	bool stepping = false;
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

		} else if (a == "--listen" && i + 1 < argc) {
			listen_at = argv[++i];

		} else if (a == "--token" && i + 1 < argc) {
			token = argv[++i];

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

		else if (a == "--step")
			stepping = true;

		else if (a == "--state" && i + 1 < argc)
			state_file = argv[++i];

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
	if (!listen_at.empty() && !listen_channel(listen_at, token, handle_command))
		rv_host_log(
			"can't listen for the REverb plugin at %s", listen_at.c_str());

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

	// Commands are read from the start: in step mode, 'next' releases load
	std::signal(SIGINT, on_signal);
	std::signal(SIGTERM, on_signal);
	if (!once || stepping) std::thread(command_thread).detach();
	set_step_mode(stepping);

	step("load");
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

	// Before activation, as a host restoring a project does
	if (!state_file.empty() && !g_quit) {
		step("state");
		load_state_file(*host, state_file);
	}

	// --exit without --blocks is just load and unload
	std::unique_ptr<Engine> engine;

	if (processing && !g_quit && !(once && blocks < 0)) {
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
			run_tasks(*host);
			pump_main_thread(10);
		}
		engine->describe();

	} else if (!once) {
		rv_host_log("plugin is alive (pid %d): 'info', 'quit'", (int)getpid());

		while (!g_quit) {
			host->idle();
			run_tasks(*host);
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

	step("unload");
	rv_host_log("unloading");
	host->unload();
	host.reset();

	return 0;
}
