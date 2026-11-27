/**
 * @file host.cpp
 * @brief CLAP for reverb-minihost: the part of the ABI it calls, and the
 * ids from exts.inc
 */
#include "formats/host.hpp"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

namespace rv::clap {
namespace {

struct clap_version {
	uint32_t major, minor, revision;
};

typedef clap_version clap_version_t;

struct clap_host;
typedef clap_host clap_host_t;
struct clap_host {
	clap_version_t clap_version;
	void* host_data;
	const char* name;
	const char* vendor;
	const char* url;
	const char* version;
	const void* (*get_extension)(const clap_host* host, const char* id);
	void (*request_restart)(const clap_host* host);
	void (*request_process)(const clap_host* host);
	void (*request_callback)(const clap_host* host);
};

struct clap_plugin_descriptor {
	clap_version_t clap_version;
	const char* id;
	const char* name;
	const char* vendor;
	const char* url;
	const char* manual_url;
	const char* support_url;
	const char* version;
	const char* description;
	const char* const* features;
};

struct clap_process;

struct clap_plugin {
	const clap_plugin_descriptor* desc;
	void* plugin_data;
	bool (*init)(const clap_plugin* plugin);
	void (*destroy)(const clap_plugin* plugin);
	bool (*activate)(const clap_plugin* plugin, double sample_rate,
		uint32_t min_frames, uint32_t max_frames);
	void (*deactivate)(const clap_plugin* plugin);
	bool (*start_processing)(const clap_plugin* plugin);
	void (*stop_processing)(const clap_plugin* plugin);
	void (*reset)(const clap_plugin* plugin);
	int32_t (*process)(const clap_plugin* plugin, const clap_process* p);
	const void* (*get_extension)(const clap_plugin* plugin, const char* id);
	void (*on_main_thread)(const clap_plugin* plugin);
};

struct clap_plugin_factory {
	uint32_t (*get_plugin_count)(const clap_plugin_factory* factory);
	const clap_plugin_descriptor* (*get_plugin_descriptor)(
		const clap_plugin_factory* factory, uint32_t index);
	const clap_plugin* (*create_plugin)(const clap_plugin_factory* factory,
		const clap_host* host, const char* plugin_id);
};

struct clap_plugin_entry {
	clap_version_t clap_version;
	bool (*init)(const char* plugin_path);
	void (*deinit)();
	const void* (*get_factory)(const char* factory_id);
};

#define RV_CLAP_FACTORY(Name, id, s)                                           \
	constexpr const char* k##Name##Factory = id;
#define RV_CLAP_EXT(Name, id, plugin_s, host_s)                                \
	constexpr const char* k##Name##Ext = id;
#include "exts.inc"
#undef RV_CLAP_FACTORY
#undef RV_CLAP_EXT

struct ExtName {
	const char* id;
	const char* name;
};
const ExtName kExtNames[] = {
#define RV_CLAP_FACTORY(Name, id, s)
#define RV_CLAP_EXT(Name, id, plugin_s, host_s) { id, #Name },
#include "exts.inc"
#undef RV_CLAP_FACTORY
#undef RV_CLAP_EXT
};

const char* ext_name(const char* id) {
	for (const ExtName& e : kExtNames)
		if (!std::strcmp(e.id, id)) return e.name;
	return "unknown";
}

// Processing

struct clap_audio_buffer {
	float** data32;
	double** data64;
	uint32_t channel_count;
	uint32_t latency;
	uint64_t constant_mask;
};

struct clap_event_header {
	uint32_t size;
	uint32_t time; ///< Frames from the start of the block
	uint16_t space_id;
	uint16_t type;
	uint32_t flags;
};

enum : uint16_t {
	CLAP_CORE_EVENT_SPACE_ID = 0,
	CLAP_EVENT_NOTE_ON = 0,
	CLAP_EVENT_NOTE_OFF = 1,
	CLAP_EVENT_TRANSPORT = 9,
};

struct clap_event_note {
	clap_event_header header;
	int32_t note_id;
	int16_t port_index;
	int16_t channel;
	int16_t key;
	double velocity;
};

constexpr int64_t CLAP_FIXEDPOINT_FACTOR = int64_t(1) << 31;

enum : uint32_t { // clap_transport_flags
	CLAP_TRANSPORT_HAS_TEMPO = 1,
	CLAP_TRANSPORT_HAS_BEATS_TIMELINE = 2,
	CLAP_TRANSPORT_HAS_SECONDS_TIMELINE = 4,
	CLAP_TRANSPORT_HAS_TIME_SIGNATURE = 8,
	CLAP_TRANSPORT_IS_PLAYING = 16,
};

struct clap_event_transport {
	clap_event_header header;
	uint32_t flags;
	int64_t song_pos_beats; ///< Fixed point, CLAP_FIXEDPOINT_FACTOR
	int64_t song_pos_seconds;
	double tempo;
	double tempo_inc;
	int64_t loop_start_beats;
	int64_t loop_end_beats;
	int64_t loop_start_seconds;
	int64_t loop_end_seconds;
	int64_t bar_start;
	int32_t bar_number;
	uint16_t tsig_num;
	uint16_t tsig_denom;
};

struct clap_input_events {
	void* ctx;
	uint32_t (*size)(const clap_input_events* list);
	const clap_event_header* (*get)(
		const clap_input_events* list, uint32_t index);
};

struct clap_output_events {
	void* ctx;
	bool (*try_push)(
		const clap_output_events* list, const clap_event_header* event);
};

struct clap_process {
	int64_t steady_time;
	uint32_t frames_count;
	const clap_event_transport* transport;
	const clap_audio_buffer* audio_inputs;
	clap_audio_buffer* audio_outputs;
	uint32_t audio_inputs_count;
	uint32_t audio_outputs_count;
	const clap_input_events* in_events;
	const clap_output_events* out_events;
};

enum : int32_t { CLAP_PROCESS_ERROR = 0 };

struct clap_audio_port_info {
	uint32_t id;
	char name[256];
	uint32_t flags;
	uint32_t channel_count;
	const char* port_type;
	uint32_t in_place_pair;
};

struct clap_plugin_audio_ports {
	uint32_t (*count)(const clap_plugin* plugin, bool is_input);
	bool (*get)(const clap_plugin* plugin, uint32_t index, bool is_input,
		clap_audio_port_info* info);
};

struct clap_plugin_note_ports {
	uint32_t (*count)(const clap_plugin* plugin, bool is_input);
	void* get;
};

// Host extensions

const char* const kSeverity[] = { "debug", "info", "warning", "error", "fatal",
	"host misbehaving", "plugin misbehaving" };

void host_log(const clap_host*, int32_t severity, const char* msg) {
	const bool known = severity >= 0 && severity < 7;
	rv_host_log(
		"plugin %s: %s", known ? kSeverity[severity] : "log", msg ? msg : "");
}

struct clap_host_log {
	void (*log)(const clap_host* host, int32_t severity, const char* msg);
} const g_host_log = { host_log };

struct clap_host_thread_check {
	bool (*is_main_thread)(const clap_host* host);
	bool (*is_audio_thread)(const clap_host* host);
} const g_thread_check = { [](const clap_host*) {
							  return host::on_main_thread();
						  },
	[](const clap_host*) { return host::on_audio_thread(); } };

std::atomic<bool> g_callback_requested{ false };

const void* host_get_extension(const clap_host*, const char* id) {
	const void* ext = nullptr;
	if (!std::strcmp(id, kLOGExt)) ext = &g_host_log;
	else if (!std::strcmp(id, kTHREAD_CHECKExt))
		ext = &g_thread_check;

	rv_host_log("host extension requested: %s (%s)%s", id, ext_name(id),
		ext ? ", provided" : "");
	return ext;
}

void host_request_restart(const clap_host*) { rv_host_log("request_restart"); }
void host_request_process(const clap_host*) { rv_host_log("request_process"); }
void host_request_callback(const clap_host*) { g_callback_requested = true; }

const clap_host_t g_host = { { 1, 2, 0 }, nullptr, "reverb-minihost", "REverb",
	"", "0.1", host_get_extension, host_request_restart, host_request_process,
	host_request_callback };

class ClapHost final : public host::PluginHost {
public:
	Interface format() const override { return Interface::CLAP; }

	bool load(host::SharedLib& lib, const host::LoadOptions& opts) override {
		// clap_entry is a data symbol: the struct itself, not a function
		entry_ = (const clap_plugin_entry*)lib.sym("clap_entry");
		if (entry_ == nullptr) {
			rv_host_log("clap_entry is not exported");
			return false;
		}

		rv_host_log("entry point: clap_entry @ %p (CLAP %u.%u.%u)",
			(const void*)entry_, entry_->clap_version.major,
			entry_->clap_version.minor, entry_->clap_version.revision);

		if (!entry_->init(opts.bundle_path.c_str())) {
			rv_host_log("clap_entry.init failed");
			entry_ = nullptr;
			return false;
		}

		inited_ = true;

		auto* factory =
			(const clap_plugin_factory*)entry_->get_factory(kPLUGINFactory);
		if (factory == nullptr) {
			rv_host_log("no %s", kPLUGINFactory);
			return false;
		}

		unsigned n = factory->get_plugin_count(factory);
		for (unsigned i = 0; i < n; ++i) {
			const clap_plugin_descriptor* d =
				factory->get_plugin_descriptor(factory, i);
			if (d) rv_host_log("  plugin %u: %s (%s)", i, d->name, d->id);
		}

		if (opts.class_index < 0 || (unsigned)opts.class_index >= n) {
			rv_host_log("no plugin #%d", opts.class_index);
			return false;
		}

		const clap_plugin_descriptor* d =
			factory->get_plugin_descriptor(factory, (unsigned)opts.class_index);
		plugin_ = d ? factory->create_plugin(factory, &g_host, d->id) : nullptr;
		if (plugin_ == nullptr) {
			rv_host_log("create_plugin failed");
			return false;
		}

		rv_host_log("clap_plugin @ %p: init=%p process=%p",
			(const void*)plugin_, (void*)plugin_->init,
			(void*)plugin_->process);

		bool ok = plugin_->init(plugin_);
		rv_host_log("clap_plugin.init -> %d", ok);

		if (!ok) {
			plugin_->destroy(plugin_);
			plugin_ = nullptr;
		}

		return ok;
	}

	void idle() override {
		if (plugin_ != nullptr && g_callback_requested.exchange(false) &&
			plugin_->on_main_thread)
			plugin_->on_main_thread(plugin_);
	}

	void describe() const override {
		if (plugin_ != nullptr)
			rv_host_log("CLAP \"%s\" by \"%s\" v%s", plugin_->desc->name,
				plugin_->desc->vendor, plugin_->desc->version);
	}

	bool activate(const host::ProcessSetup& s) override {
		if (plugin_ == nullptr) return false;

		auto* ports = (const clap_plugin_audio_ports*)plugin_->get_extension(
			plugin_, kAUDIO_PORTSExt);
		ins_ = ports_of(ports, true, s.block_size, &in_bufs_);
		outs_ = ports_of(ports, false, s.block_size, &out_bufs_);
		if (ports == nullptr) rv_host_log("no audio ports extension");

		auto* notes = (const clap_plugin_note_ports*)plugin_->get_extension(
			plugin_, kNOTE_PORTSExt);
		note_in_ = notes != nullptr && notes->count(plugin_, true) > 0;
		rv_host_log("note input: %s", note_in_ ? "yes" : "no");

		active_ = plugin_->activate(plugin_, s.sample_rate, 1, s.block_size);
		rv_host_log("clap_plugin.activate -> %d", active_);
		return active_;
	}

	bool start_processing() override {
		const bool ok = plugin_->start_processing(plugin_);
		rv_host_log("clap_plugin.start_processing -> %d", ok);
		return ok;
	}

	float process(const host::Block& b) override {
		for (host::Channels& ch : ins_)
			ch.fill(b.input, b.frames);
		for (host::Channels& ch : outs_)
			ch.clear(b.frames);

		num_events_ = 0;
		if (note_in_)
			for (size_t i = 0; i < b.num_notes && i < 16; i++) {
				const host::NoteEvent& n = b.notes[i];
				clap_event_note& e = notes_[num_events_++];
				e = {};
				e.header = { sizeof(clap_event_note), n.offset,
					CLAP_CORE_EVENT_SPACE_ID,
					n.on ? CLAP_EVENT_NOTE_ON : CLAP_EVENT_NOTE_OFF, 0 };
				e.note_id = -1;
				e.key = n.key;
				e.velocity = n.on ? n.velocity : 0.0;
			}

		const double seconds = b.sample_pos / b.sample_rate;
		const double beats = b.ppq();
		clap_event_transport t = {};
		t.header = { sizeof(clap_event_transport), 0, CLAP_CORE_EVENT_SPACE_ID,
			CLAP_EVENT_TRANSPORT, 0 };
		t.flags = CLAP_TRANSPORT_HAS_TEMPO | CLAP_TRANSPORT_HAS_BEATS_TIMELINE |
				  CLAP_TRANSPORT_HAS_SECONDS_TIMELINE |
				  CLAP_TRANSPORT_HAS_TIME_SIGNATURE | CLAP_TRANSPORT_IS_PLAYING;
		t.song_pos_beats = int64_t(beats * CLAP_FIXEDPOINT_FACTOR);
		t.song_pos_seconds = int64_t(seconds * CLAP_FIXEDPOINT_FACTOR);
		t.tempo = b.tempo;
		t.bar_start =
			int64_t(std::floor(beats / 4.0) * 4.0 * CLAP_FIXEDPOINT_FACTOR);
		t.bar_number = int32_t(beats / 4.0);
		t.tsig_num = 4;
		t.tsig_denom = 4;

		const clap_input_events in_events = { this, &events_size, &events_get };
		const clap_output_events out_events = { this, &events_push };

		clap_process p = {};
		p.steady_time = b.sample_pos;
		p.frames_count = b.frames;
		p.transport = &t;
		p.audio_inputs = in_bufs_.empty() ? nullptr : in_bufs_.data();
		p.audio_outputs = out_bufs_.empty() ? nullptr : out_bufs_.data();
		p.audio_inputs_count = uint32_t(in_bufs_.size());
		p.audio_outputs_count = uint32_t(out_bufs_.size());
		p.in_events = &in_events;
		p.out_events = &out_events;

		const int32_t status = plugin_->process(plugin_, &p);
		if (status == CLAP_PROCESS_ERROR && !process_failed_) {
			rv_host_log("process -> CLAP_PROCESS_ERROR (only the first is "
						"logged)");
			process_failed_ = true;
		}

		float peak = 0.f;
		for (const host::Channels& ch : outs_)
			peak = std::max(peak, ch.peak(b.frames));
		return peak;
	}

	void stop_processing() override { plugin_->stop_processing(plugin_); }

	void deactivate() override {
		if (active_) plugin_->deactivate(plugin_);
		active_ = false;
	}

	void unload() override {
		if (plugin_ != nullptr) {
			plugin_->destroy(plugin_);
			plugin_ = nullptr;
		}
		if (inited_ && entry_ != nullptr) entry_->deinit();
		inited_ = false;
	}

private:
	/// Buffers for the plugin's input or output ports; none without the
	/// audio-ports extension
	std::vector<host::Channels> ports_of(const clap_plugin_audio_ports* ports,
		bool is_input, uint32_t frames, std::vector<clap_audio_buffer>* bufs) {
		std::vector<host::Channels> out;
		const uint32_t n = ports ? ports->count(plugin_, is_input) : 0;
		out.resize(n);
		for (uint32_t i = 0; i < n; i++) {
			clap_audio_port_info info = {};
			const uint32_t ch = ports->get(plugin_, i, is_input, &info)
									? info.channel_count
									: 0;
			out[i].resize(ch, frames);
			rv_host_log("  audio %s port %u \"%s\": %u channels",
				is_input ? "input" : "output", i, info.name, ch);
		}
		bufs->assign(n, clap_audio_buffer{});
		for (uint32_t i = 0; i < n; i++)
			(*bufs)[i] = { out[i].ptrs(), nullptr, out[i].count(), 0, 0 };
		return out;
	}

	static uint32_t events_size(const clap_input_events* l) {
		return ((const ClapHost*)l->ctx)->num_events_;
	}
	static const clap_event_header* events_get(
		const clap_input_events* l, uint32_t i) {
		const ClapHost* h = (const ClapHost*)l->ctx;
		return i < h->num_events_ ? &h->notes_[i].header : nullptr;
	}
	static bool events_push(
		const clap_output_events*, const clap_event_header*) {
		return true; // parameter changes and note ends: dropped for now
	}

	std::vector<host::Channels> ins_, outs_;
	std::vector<clap_audio_buffer> in_bufs_, out_bufs_;
	clap_event_note notes_[16] = {};
	uint32_t num_events_ = 0;
	bool note_in_ = false;
	bool active_ = false;
	bool process_failed_ = false;

	const clap_plugin_entry* entry_ = nullptr;
	const clap_plugin* plugin_ = nullptr;
	bool inited_ = false;
};

std::unique_ptr<host::PluginHost> create() {
	return std::make_unique<ClapHost>();
}

} // namespace

const HostModule host_module = { Interface::CLAP, &create };

} // namespace rv::clap
