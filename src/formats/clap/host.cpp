/**
 * @file host.cpp
 * @brief CLAP for reverb-minihost: the host and its extensions, and the
 * plugin's lifecycle (the ABI is in host_abi.hpp)
 */
#include "formats/host.hpp"
#include "host_abi.hpp"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace rv::clap {
namespace {

using namespace abi;

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

// A blob being read by the plugin
struct ReadStream {
	const std::vector<uint8_t>* data;
	size_t pos;
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

// The plugin tells about its parameters and state: logged only
struct clap_host_params {
	void (*rescan)(const clap_host* host, uint32_t flags);
	void (*clear)(const clap_host* host, uint32_t id, uint32_t flags);
	void (*request_flush)(const clap_host* host);
} const g_host_params = {
	[](const clap_host*, uint32_t flags) {
		rv_host_log("params.rescan(flags=%x)", flags);
	},
	[](const clap_host*, uint32_t id, uint32_t flags) {
		rv_host_log("params.clear(#%u, flags=%x)", id, flags);
	},
	[](const clap_host*) {} // flushed with the next block, or by set
};

struct clap_host_state {
	void (*mark_dirty)(const clap_host* host);
} const g_host_state = { [](const clap_host*) {
	static std::atomic<int> n{ 0 };
	if (n++ < 3) rv_host_log("state.mark_dirty (logged the first 3 times)");
} };

const void* host_get_extension(const clap_host*, const char* id) {
	const void* ext = nullptr;
	if (!std::strcmp(id, kLOGExt)) ext = &g_host_log;
	else if (!std::strcmp(id, kTHREAD_CHECKExt))
		ext = &g_thread_check;
	else if (!std::strcmp(id, kPARAMSExt))
		ext = &g_host_params;
	else if (!std::strcmp(id, kSTATEExt))
		ext = &g_host_state;

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

		host::step("entry");
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
		host::step("create");
		plugin_ = d ? factory->create_plugin(factory, &g_host, d->id) : nullptr;
		if (plugin_ == nullptr) {
			rv_host_log("create_plugin failed");
			return false;
		}

		rv_host_log("clap_plugin @ %p: init=%p process=%p",
			(const void*)plugin_, (void*)plugin_->init,
			(void*)plugin_->process);

		host::step("init");
		bool ok = plugin_->init(plugin_);
		rv_host_log("clap_plugin.init -> %d", ok);

		if (!ok) {
			plugin_->destroy(plugin_);
			plugin_ = nullptr;
			return false;
		}

		// Asked for after init, as the spec says
		params_ = (const clap_plugin_params*)plugin_->get_extension(
			plugin_, kPARAMSExt);
		state_ = (const clap_plugin_state*)plugin_->get_extension(
			plugin_, kSTATEExt);

		rv_host_log("params: %s, state: %s", params_ ? "yes" : "no",
			state_ ? "yes" : "no");
		return true;
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

		// Parameter changes first, at frame 0, then the notes in order
		num_events_ = 0;
		pending_.take(&changes_);
		size_t np = 0;
		for (const host::ParamQueue::Change& c : changes_) {
			if (np == kMaxParamEvents) break;
			param_events_[np] = param_event(c.id, c.value);
			events_[num_events_++] = &param_events_[np++].header;
		}

		if (np < changes_.size())
			rv_host_log("%zu parameter changes dropped: %zu per block at most",
				changes_.size() - np, kMaxParamEvents);

		size_t nn = 0;
		if (note_in_)
			for (size_t i = 0; i < b.num_notes && i < 16; i++) {
				const host::NoteEvent& n = b.notes[i];
				clap_event_note& e = notes_[nn++];
				events_[num_events_++] = &e.header;
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

		EventList list = { events_, num_events_ };
		const clap_input_events in_events = { &list, &events_size,
			&events_get };
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

	size_t param_count() const override {
		return params_ ? params_->count(plugin_) : 0;
	}

	bool param(size_t index, host::Param* out) const override {
		clap_param_info info = {};
		if (params_ == nullptr ||
			!params_->get_info(plugin_, uint32_t(index), &info))
			return false;

		out->id = info.id;
		out->name = info.module[0] ? std::string(info.module) + "/" + info.name
								   : std::string(info.name);
		out->stepped = (info.flags & CLAP_PARAM_IS_STEPPED) != 0;
		double v = info.default_value;
		params_->get_value(plugin_, info.id, &v);
		const double range = info.max_value - info.min_value;
		out->value = range > 0 ? (v - info.min_value) / range : 0.0;

		char text[256] = {};
		out->text =
			params_->value_to_text && params_->value_to_text(plugin_, info.id,
										  v, text, sizeof(text))
				? text
				: std::to_string(v);
		return true;
	}

	bool set_param(uint32_t id, double value) override {
		clap_param_info info = {};
		if (!find_info(id, &info)) return false;
		double plain =
			info.min_value + value * (info.max_value - info.min_value);
		if (info.flags & CLAP_PARAM_IS_STEPPED) plain = std::round(plain);

		// Active, the plugin takes events on the audio thread only
		if (active_) {
			pending_.push(id, plain);
			rv_host_log("queued: applies with the next block");
			return true;
		}

		clap_event_param_value e = param_event(id, plain);
		const clap_event_header* h = &e.header;

		EventList list = { &h, 1 };
		const clap_input_events in = { &list, &events_size, &events_get };
		const clap_output_events out = { this, &events_push };

		params_->flush(plugin_, &in, &out);

		return true;
	}

	bool save_state(std::vector<uint8_t>* out) override {
		if (state_ == nullptr) {
			rv_host_log("the plugin has no state extension");
			return false;
		}

		out->clear();
		const clap_ostream s = { out,
			[](const clap_ostream* s, const void* buf, uint64_t size) {
				auto* v = (std::vector<uint8_t>*)s->ctx;
				v->insert(
					v->end(), (const uint8_t*)buf, (const uint8_t*)buf + size);
				return int64_t(size);
			} };

		const bool ok = state_->save(plugin_, &s);
		rv_host_log("clap_plugin_state.save -> %d", ok);
		return ok;
	}

	bool load_state(const std::vector<uint8_t>& data) override {
		if (state_ == nullptr) {
			rv_host_log("the plugin has no state extension");
			return false;
		}

		ReadStream rs = { &data, 0 };
		const clap_istream s = { &rs,
			[](const clap_istream* s, void* buf, uint64_t size) {
				auto* r = (ReadStream*)s->ctx;
				const size_t n =
					std::min<size_t>(size_t(size), r->data->size() - r->pos);
				std::memcpy(buf, r->data->data() + r->pos, n);

				r->pos += n;
				return int64_t(n); // 0 at the end
			} };

		const bool ok = state_->load(plugin_, &s);
		rv_host_log("clap_plugin_state.load -> %d (%zu of %zu bytes read)", ok,
			rs.pos, data.size());
		return ok;
	}

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

	// An input event list: the events, sorted by time
	struct EventList {
		const clap_event_header* const* events;
		uint32_t count;
	};

	static uint32_t events_size(const clap_input_events* l) {
		return ((const EventList*)l->ctx)->count;
	}
	static const clap_event_header* events_get(
		const clap_input_events* l, uint32_t i) {
		const EventList* e = (const EventList*)l->ctx;
		return i < e->count ? e->events[i] : nullptr;
	}

	static clap_event_param_value param_event(uint32_t id, double plain) {
		clap_event_param_value e = {};
		e.header = { sizeof(clap_event_param_value), 0,
			CLAP_CORE_EVENT_SPACE_ID, CLAP_EVENT_PARAM_VALUE, 0 };
		e.param_id = id;
		e.note_id = e.port_index = e.channel = e.key = -1; // every one
		e.value = plain;
		return e;
	}

	bool find_info(uint32_t id, clap_param_info* info) const {
		const size_t n = param_count();
		for (size_t i = 0; i < n; i++)
			if (params_->get_info(plugin_, uint32_t(i), info) && info->id == id)
				return true;
		return false;
	}
	static bool events_push(
		const clap_output_events*, const clap_event_header*) {
		return true; // parameter changes and note ends: dropped for now
	}

	std::vector<host::Channels> ins_, outs_;
	std::vector<clap_audio_buffer> in_bufs_, out_bufs_;
	static constexpr size_t kMaxParamEvents = 64;
	clap_event_note notes_[16] = {};
	clap_event_param_value param_events_[kMaxParamEvents] = {};
	const clap_event_header* events_[kMaxParamEvents + 16] = {};
	uint32_t num_events_ = 0;
	host::ParamQueue pending_;
	std::vector<host::ParamQueue::Change> changes_;
	const clap_plugin_params* params_ = nullptr;
	const clap_plugin_state* state_ = nullptr;
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
