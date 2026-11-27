#include "engine.hpp"
#include "detect.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>

namespace rv::host {

// Threads

static thread_local bool t_main = false;
static thread_local bool t_audio = false;

bool on_main_thread() { return t_main; }
bool on_audio_thread() { return t_audio; }
void mark_main_thread() { t_main = true; }

// Channels

void Channels::resize(uint32_t channels, uint32_t frames) {
	data_.assign(channels, std::vector<float>(frames, 0.f));
	ptrs_.resize(channels);
	for (uint32_t c = 0; c < channels; c++)
		ptrs_[c] = data_[c].data();
}

void Channels::fill(const float* src, uint32_t frames) {
	for (auto& ch : data_)
		std::copy(src, src + frames, ch.begin());
}

void Channels::clear(uint32_t frames) {
	for (auto& ch : data_)
		std::fill(ch.begin(), ch.begin() + frames, 0.f);
}

float Channels::peak(uint32_t frames) const {
	float p = 0.f;
	for (const auto& ch : data_)
		for (uint32_t i = 0; i < frames; i++)
			p = std::max(p, std::fabs(ch[i]));
	return p;
}

// Engine

std::optional<Signal> parse_signal(const std::string& name) {

	switch (str_cth(name.c_str())) {
	case str_cth("silence"): return Signal::SILENCE;
	case str_cth("impulse"): return Signal::IMPULSE;
	case str_cth("sine"):	 return Signal::SINE;
	case str_cth("noise"):	 return Signal::NOISE;
	default:				 return std::nullopt;
	}
}

static const char* signal_name(Signal s) {
	switch (s) {
	case Signal::SILENCE: return "silence";
	case Signal::IMPULSE: return "impulse";
	case Signal::SINE:	  return "sine";
	default:			  return "noise";
	}
}

static const double TWO_PI = 6.283185307179586;

static double to_db(float v) {
	return v > 0.f ? 20.0 * std::log10(v) : -INFINITY;
}

Engine::Engine(PluginHost& host, const EngineOptions& opts)
	: host_(host), opts_(opts) {}

Engine::~Engine() { stop(); }

bool Engine::start() {
	const ProcessSetup& s = opts_.setup;
	step("activate");
	if (!host_.activate(s)) {
		rv_host_log("the plugin can't be activated: not processing");
		return false;
	}

	active_ = true;
	input_.assign(s.block_size, 0.f);
	quit_ = false;
	thread_ = std::thread(&Engine::thread_main, this);

	rv_host_log("activated at %.0f Hz, %u frames per block; input %s%s",
		s.sample_rate, s.block_size, signal_name(opts_.signal),
		opts_.notes ? ", a note every second" : "");
	return true;
}

void Engine::stop() {
	if (thread_.joinable()) {
		quit_ = true;
		thread_.join();
	}

	if (active_) {
		step("deactivate");
		host_.deactivate();
		active_ = false;
		rv_host_log("deactivated after %llu blocks",
			(unsigned long long)blocks_.load());
	}
}

void Engine::describe() const {
	if (!active_) {
		rv_host_log("not processing");
		return;
	}

	const int64_t b = budget_;
	char state[48];

	if (b < 0) std::snprintf(state, sizeof(state), "playing");
	else if (b == 0)
		std::snprintf(state, sizeof(state), "paused");
	else
		std::snprintf(state, sizeof(state), "%lld blocks to go", (long long)b);

	rv_host_log("engine %s, %llu blocks run, output peak %.1f dBFS", state,
		(unsigned long long)blocks_.load(), to_db(peak_.load()));
}

void Engine::make_block(
	int64_t pos, uint32_t frames, std::vector<NoteEvent>* notes) {

	const double sr = opts_.setup.sample_rate;
	const int64_t second = int64_t(sr);

	for (uint32_t i = 0; i < frames; i++) {
		const int64_t t = pos + i;
		float v = 0.f;

		switch (opts_.signal) {
		case Signal::SILENCE: break;
		case Signal::IMPULSE: v = (t % second == 0) ? 1.f : 0.f; break;
		case Signal::SINE:
			// 440 Hz; the phase is wrapped so it stays precise in long runs
			v = 0.25f * float(std::sin(
							TWO_PI * std::fmod(440.0 * double(t) / sr, 1.0)));
			break;

		case Signal::NOISE:
			noise_ ^= noise_ << 13, noise_ ^= noise_ >> 17,
				noise_ ^= noise_ << 5;
			v = 0.25f * (float(noise_) / 4294967296.f * 2.f - 1.f);
			break;
		}

		input_[i] = v;
	}

	notes->clear();
	if (!opts_.notes) return;
	// On at every whole second, off at every half
	const int64_t half = second / 2;
	for (int64_t t = (pos + half - 1) / half * half; t < pos + frames;
		t += half)
		notes->push_back({ uint32_t(t - pos), 60, 0.8f, (t % second) == 0 });
}

void Engine::thread_main() {
	t_audio = true;
	using clock = std::chrono::steady_clock;
	const ProcessSetup s = opts_.setup;
	const auto period = std::chrono::duration_cast<clock::duration>(
		std::chrono::duration<double>(s.block_size / s.sample_rate));

	std::vector<NoteEvent> notes;

	int64_t pos = 0;
	float wnd_peak = 0.f, last_wnd_peak = 0.f;
	int64_t wnd_frames = 0;

	bool log_first = false;

	auto last = clock::now();

	while (!quit_) {
		const int64_t b = budget_;
		if (b == 0) {
			std::this_thread::sleep_for(std::chrono::milliseconds(10));
			last = clock::now();
			continue;
		}

		if (!started_) {
			step("start");
			if (!host_.start_processing()) {
				rv_host_log("start_processing was refused, pausing");
				budget_ = 0;
				continue;
			}
			started_ = true;
		}

		if (pos == 0) step("process");
		make_block(pos, s.block_size, &notes);
		const Block block{ s.block_size, pos, s.sample_rate, opts_.tempo,
			input_.data(), notes.data(), notes.size() };
		const float peak = host_.process(block);

		pos += s.block_size;
		blocks_++;

		// The peak of the last full second and the one under way, so that a
		// short run reports one too
		wnd_peak = std::max(wnd_peak, peak);
		wnd_frames += s.block_size;
		peak_ = std::max(last_wnd_peak, wnd_peak);

		if (wnd_frames >= int64_t(s.sample_rate)) {
			last_wnd_peak = wnd_peak;
			wnd_peak = 0.f;
			wnd_frames = 0;
		}

		if (!log_first) {
			rv_host_log(
				"first block processed, output peak %.1f dBFS", to_db(peak));
			log_first = true;
		}

		if (b > 0) {
			int64_t cur = b;
			// Only count down a budget nobody changed meanwhile
			if (budget_.compare_exchange_strong(cur, b - 1) && b == 1)
				rv_host_log("ran the requested blocks, paused");
		}

		if (opts_.realtime) {
			last += period;
			const auto now = clock::now();
			if (now < last) std::this_thread::sleep_until(last);

			// Stop racing if far behind (e.g. after a breakpoint)
			else if (now - last > std::chrono::milliseconds(100))
				last = now;
		}
	}

	if (started_) {
		step("stop");
		host_.stop_processing();
		started_ = false;
	}
}

} // namespace rv::host
