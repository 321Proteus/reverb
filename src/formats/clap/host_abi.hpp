#pragma once
/**
 * @file host_abi.hpp
 * @brief The part of the CLAP ABI reverb-minihost calls, declared by hand,
 * and the factory and extension ids of exts.inc
 */
#include <cstdint>

namespace rv::clap::abi {

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
	CLAP_EVENT_PARAM_VALUE = 5,
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

struct clap_event_param_value {
	clap_event_header header;
	uint32_t param_id;
	void* cookie;
	int32_t note_id;
	int16_t port_index;
	int16_t channel;
	int16_t key;
	double value;
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

// Parameters and state

enum : uint32_t { CLAP_PARAM_IS_STEPPED = 1 };

struct clap_param_info {
	uint32_t id;
	uint32_t flags;
	void* cookie;
	char name[256];
	char module[1024];
	double min_value;
	double max_value;
	double default_value;
};

struct clap_plugin_params {
	uint32_t (*count)(const clap_plugin* plugin);
	bool (*get_info)(
		const clap_plugin* plugin, uint32_t index, clap_param_info* info);
	bool (*get_value)(const clap_plugin* plugin, uint32_t id, double* value);
	bool (*value_to_text)(const clap_plugin* plugin, uint32_t id, double value,
		char* out, uint32_t capacity);
	bool (*text_to_value)(
		const clap_plugin* plugin, uint32_t id, const char* text, double* out);
	void (*flush)(const clap_plugin* plugin, const clap_input_events* in,
		const clap_output_events* out);
};

struct clap_ostream {
	void* ctx;
	int64_t (*write)(const clap_ostream* s, const void* buffer, uint64_t size);
};
struct clap_istream {
	void* ctx;
	int64_t (*read)(const clap_istream* s, void* buffer, uint64_t size);
};

struct clap_plugin_state {
	bool (*save)(const clap_plugin* plugin, const clap_ostream* stream);
	bool (*load)(const clap_plugin* plugin, const clap_istream* stream);
};

} // namespace rv::clap::abi
