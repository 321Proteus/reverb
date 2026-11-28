#pragma once
/**
 * @file host_objects.hpp
 * @brief The VST3 objects reverb-minihost implements for the plugin: the host
 * application, the event and parameter change lists, the state stream and
 * the component handler
 *
 * They live as long as the host does: reference counts are ignored.
 */
#include "host_abi.hpp"
#include <string>
#include <vector>

namespace rv::vst3::abi {

// The input IEventList: the block's notes, owned by the host
struct HostEventList {
	IEventList base;
	Event events[16];
	int32 count;
};

// A state blob in memory, as an IBStream
struct MemStream {
	IBStream base;
	std::vector<uint8_t> data;
	int64 pos = 0;
};

// The block's parameter changes: one point each, at frame 0
constexpr int32 kMaxChanges = 64;

struct HostQueue {
	IParamValueQueue base;
	ParamID id;
	ParamValue value;
};

struct HostChanges {
	IParameterChanges base;
	HostQueue queues[kMaxChanges];
	int32 count;
};

/** @brief The context initialize() gets */
extern IHostApplication g_host_app;
/** @brief Edits the plugin reports, from its GUI or automation: logged */
extern IComponentHandler g_handler;

extern IEventList_vtbl g_events_vtbl;
extern IBStream_vtbl g_stream_vtbl;
extern IParamValueQueue_vtbl g_queue_vtbl;
extern IParameterChanges_vtbl g_changes_vtbl;

/** @brief A String128 as UTF-8 */
std::string utf8(const TChar* s, size_t max = 128);

} // namespace rv::vst3::abi
