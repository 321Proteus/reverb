#pragma once
#include "host.hpp"
#include <atomic>
#include <cstdint>
#include <thread>

/**
 * @file engine.hpp
 * @brief Drives a PluginHost's processing from an audio thread: a test signal
 * a repeating note for instruments, and no audio device
 */
namespace rv::host {

/** @brief Test feed samples for plugin input
 * @todo actual audio processing
 */
enum class Signal { SILENCE, IMPULSE, SINE, NOISE };

/** @brief Parses "silence", "impulse", "sine" or "noise" */
std::optional<Signal> parse_signal(const std::string& name);

struct EngineOptions {
	ProcessSetup setup;
	Signal signal = Signal::SINE;
	/// A note on every second, off half a second later: instruments are silent
	/// without notes, effects ignore them
	bool notes = true;
	/// Paces the blocks like an audio device would; off runs them flat out
	bool realtime = true;
	double tempo = 120;
};

/**
 * @brief The audio thread and its controls
 *
 * The thread runs blocks while it has a budget: play() makes it unlimited,
 * pause() zero, run_blocks() a number of blocks. The controls are safe to call
 * from any thread.
 */
class Engine {
public:
	Engine(PluginHost& host, const EngineOptions& opts);
	~Engine();
	Engine(const Engine&) = delete;
	Engine& operator=(const Engine&) = delete;

	/// Activates the plugin and starts the audio thread, paused
	bool start();
	/// Stops the audio thread and deactivates the plugin
	void stop();

	void play() { budget_ = -1; }
	void pause() { budget_ = 0; }
	void run_blocks(int64_t n) { budget_ = n > 0 ? n : 0; }
	/// True while blocks are being run
	bool busy() const { return budget_ != 0; }

	/// Logs the state, blocks run and the recent output peak
	void describe() const;

private:
	void thread_main();
	void make_block(
		int64_t pos, uint32_t frames, std::vector<NoteEvent>* notes);

	PluginHost& host_;
	EngineOptions opts_;
	std::thread thread_;
	bool active_ = false;

	std::atomic<bool> quit_{ false };
	std::atomic<int64_t> budget_{ 0 }; ///< <0 unlimited, 0 paused
	std::atomic<uint64_t> blocks_{ 0 };
	std::atomic<float> peak_{ 0.f };	 ///< Of the last second of output
	std::atomic<bool> started_{ false }; ///< start_processing() succeeded

	std::vector<float> input_;
	uint32_t noise_ = 0x12345678;
};

} // namespace rv::host
