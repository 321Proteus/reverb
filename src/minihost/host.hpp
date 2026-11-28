#pragma once
#include "core/reverb.hpp"
#include "platform.hpp"
#include <cstdint>
#include <cstdio>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

/**
 * @file host.hpp
 * @brief The per-format host interface of reverb-minihost.
 */

/**
 * @brief reverb-minihost's counterpart of rv_log: one flushed line on stdout,
 * in the log file when one is set (see rv::host::set_log_file()) and on the
 * plugin's channel when connected (see rv::host::connect_channel())
 */
#if defined(__GNUC__) || defined(__clang__)
__attribute__((format(printf, 1, 2)))
#endif
void rv_host_log(const char* fmt, ...);

namespace rv::host {

/**
 * @brief How the user asked for the plugin to be loaded
 */
struct LoadOptions {
	std::string binary_path; ///< The module to load
	/// Its bundle dir (X.vst3, X.clap), or binary_path for a bare module
	std::string bundle_path;
	int class_index = 0; ///< Which class/plugin of a multi-plugin module
};

/**
 * @brief How the engine runs the plugin
 */
struct ProcessSetup {
	double sample_rate = 48000;
	uint32_t block_size = 512; ///< The most frames a block has
};

/**
 * @brief A note on or off for the plugin, inside a block
 */
struct NoteEvent {
	uint32_t offset; ///< Frames from the start of the block
	int16_t key;	 ///< MIDI key
	float velocity;	 ///< 0..1
	bool on;
};

/**
 * @brief One block for PluginHost::process(), made by the engine
 */
struct Block {
	uint32_t frames;
	int64_t sample_pos; ///< Of the first frame, since processing started
	double sample_rate;
	double tempo;			///< BPM, in 4/4
	const float* input;		///< @c frames samples of the test signal
	const NoteEvent* notes; ///< Sorted by offset
	size_t num_notes;

	/// Position of the first frame in quarter notes
	double ppq() const { return sample_pos / sample_rate * tempo / 60.0; }
};

/**
 * @brief Buffers of some channels, for a host to hand to the plugin
 */
class Channels {
public:
	void resize(uint32_t channels, uint32_t frames);
	uint32_t count() const { return uint32_t(ptrs_.size()); }
	float** ptrs() { return ptrs_.data(); }
	/// Copies @p src to every channel
	void fill(const float* src, uint32_t frames);
	void clear(uint32_t frames);
	/// The largest magnitude of the first @p frames of every channel
	float peak(uint32_t frames) const;

private:
	std::vector<std::vector<float>> data_;
	std::vector<float*> ptrs_;
};

/// True on the thread that runs main() (see mark_main_thread)
bool on_main_thread();
/// True on the engine's audio thread
bool on_audio_thread();
/// Marks the calling thread as the main one
void mark_main_thread();

/**
 * @brief A format-specific host: runs the module entry point, creates a
 * plugin instance, takes it through its processing lifecycle, and tears it
 * all down in the reverse order
 *
 * The engine calls activate() and deactivate() on the main thread, and
 * start_processing(), process() and stop_processing() on its audio thread.
 */
class PluginHost {
public:
	virtual ~PluginHost() = default;
	virtual Interface format() const = 0;
	/// Runs the entry point and creates the instance
	virtual bool load(SharedLib& lib, const LoadOptions& opts) = 0;
	/// Main thread tick while the plugin is kept alive
	virtual void idle() {}
	/// Prints what the user can interact with (the info command)
	virtual void describe() const {}

	/// Readies the instance to process at @p setup; false if it can't
	virtual bool activate(const ProcessSetup& setup) { return false; }
	/// Before the first process(); false if the plugin refused
	virtual bool start_processing() { return true; }
	/// Runs one block
	/// @return the peak of the plugin's output
	virtual float process(const Block& block) { return 0.f; }
	/// After the last process()
	virtual void stop_processing() {}
	/// Undoes activate()
	virtual void deactivate() {}

	/// Undoes load(), also after a partial one
	virtual void unload() = 0;
};

/**
 * @brief Connects to the REverb plugin at @p address. The
 * log goes there too, and each line it sends is passed to @p on_line, on a
 * thread of its own
 * @param address the target address ("127.0.0.1:<port>")
 * @param on_line the callback to execute the sent lines to
 * @return false if the connection failed
 */
bool connect_channel(const std::string& address,
	std::function<void(const std::string&)> on_line);

/**
 * @brief Listens at @p address for the REverb plugin, for a remote target:
 * the plugin connects to it, as it can reach the target's debug server.
 * Until then the log is kept, and sent once it has connected.
 *
 * @param address "ip:port". the ip defaults to 0.0.0.0 (every interface)
 * @param token what the plugin's first line ("hello <token>") must carry
 * @param on_line the callback each line it sends goes to, on a thread of its
 * own
 * @return false if the address can't be listened at
 */
bool listen_channel(const std::string& address, const std::string& token,
	std::function<void(const std::string&)> on_line);

/**
 * @brief Also writes the log to @p path (truncated first): the REverb plugin
 * follows it and shows it in IDA's Output window
 *
 * @return false if the file can't be opened
 */
bool set_log_file(const char* path);

/**
 * @brief Parses a case-insensitive format name
 */
std::optional<Interface> parse_interface(const std::string& name);

/** @brief Turns step mode on or off; off also releases a waiting step */
void set_step_mode(bool on);

/**
 * @brief In step mode, waits for step_next() before @p phase; returns at once
 * otherwise. Safe on any thread; the main thread's run loop keeps going.
 */
void step(const char* phase);

/** @brief Releases the waiting step, false if none was waiting */
bool step_next();

/** @brief The phase a step waits for, or null */
const char* step_waiting();

} // namespace rv::host
