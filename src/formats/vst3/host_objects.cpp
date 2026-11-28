/**
 * @file host_objects.cpp
 * @brief The VST3 objects reverb-minihost implements, as raw vtables
 */
#include "host_objects.hpp"
#include "minihost/host.hpp"
#include <algorithm>
#include <atomic>

namespace rv::vst3::abi {

// Minimal IHostApplication, handed to initialize() as the context

tresult RV_PLUGIN_API host_qi(
	IHostApplication* self, const char* iid, void** obj) {
	if (IID_FUnknown == iid || IID_IHostApplication == iid) {
		*obj = self;
		return kResultOk;
	}

	*obj = nullptr;
	return kNoInterface;
}

uint32 RV_PLUGIN_API host_ref(IHostApplication*) { return 1; }

tresult RV_PLUGIN_API host_get_name(IHostApplication*, TChar* name) {
	const char* n = "reverb-minihost";
	int i = 0;
	for (; n[i] && i < 127; ++i)
		name[i] = (TChar)n[i];
	name[i] = 0;

	return kResultOk;
}

tresult RV_PLUGIN_API host_create_instance(
	IHostApplication*, char*, char*, void** obj) {
	*obj = nullptr;
	return kNoInterface; // no IMessage / IAttributeList yet
}

IHostApplication_vtbl g_host_vtbl = { host_qi, host_ref, host_ref,
	host_get_name, host_create_instance };
IHostApplication g_host_app = { &g_host_vtbl };

tresult RV_PLUGIN_API events_qi(IEventList* self, const char* iid, void** obj) {
	if (IID_FUnknown == iid || IID_IEventList == iid) {
		*obj = self;
		return kResultOk;
	}
	*obj = nullptr;
	return kNoInterface;
}

uint32 RV_PLUGIN_API events_ref(IEventList*) { return 1; }

int32 RV_PLUGIN_API events_count(IEventList* self) {
	return ((HostEventList*)self)->count;
}

tresult RV_PLUGIN_API events_get(IEventList* self, int32 index, Event* e) {
	const HostEventList* l = (const HostEventList*)self;
	if (index < 0 || index >= l->count || e == nullptr) return kResultFalse;
	*e = l->events[index];
	return kResultOk;
}

tresult RV_PLUGIN_API events_add(IEventList*, Event*) {
	return kResultFalse; // an input list
}

IEventList_vtbl g_events_vtbl = { events_qi, events_ref, events_ref,
	events_count, events_get, events_add };

// The host objects below live as long as the host: reference counts are
// ignored, as for g_host_app
template <class T> uint32 RV_PLUGIN_API no_ref(T*) { return 1; }

// Answers queryInterface for an object implementing @p iid only
template <class T>
tresult query(T* self, const char* iid, const Iid& own, void** obj) {
	if (IID_FUnknown == iid || own == iid) {
		*obj = self;
		return kResultOk;
	}
	*obj = nullptr;
	return kNoInterface;
}

tresult RV_PLUGIN_API stream_qi(IBStream* self, const char* iid, void** obj) {
	return query(self, iid, IID_IBStream, obj);
}

tresult RV_PLUGIN_API stream_read(
	IBStream* self, void* buffer, int32 n, int32* done) {
	MemStream* m = (MemStream*)self;

	const int64 left = int64(m->data.size()) - m->pos;
	const int32 k =
		n < 0 ? 0 : int32(std::min<int64>(n, std::max<int64>(left, 0)));

	if (k > 0) std::memcpy(buffer, m->data.data() + m->pos, size_t(k));

	m->pos += k;
	if (done) *done = k;

	return kResultOk;
}

tresult RV_PLUGIN_API stream_write(
	IBStream* self, void* buffer, int32 n, int32* done) {
	MemStream* m = (MemStream*)self;
	if (n < 0) return kResultFalse;

	if (size_t(m->pos) + size_t(n) > m->data.size())
		m->data.resize(size_t(m->pos) + size_t(n));

	std::memcpy(m->data.data() + m->pos, buffer, size_t(n));
	m->pos += n;
	if (done) *done = n;

	return kResultOk;
}

tresult RV_PLUGIN_API stream_seek(
	IBStream* self, int64 pos, int32 mode, int64* result) {
	MemStream* m = (MemStream*)self;

	const int64 base = mode == 1   ? m->pos
					   : mode == 2 ? int64(m->data.size())
								   : 0;
	if (base + pos < 0) return kResultFalse;

	m->pos = base + pos;
	if (result) *result = m->pos;
	return kResultOk;
}

tresult RV_PLUGIN_API stream_tell(IBStream* self, int64* pos) {
	if (pos) *pos = ((MemStream*)self)->pos;
	return kResultOk;
}

IBStream_vtbl g_stream_vtbl = { stream_qi, no_ref<IBStream>, no_ref<IBStream>,
	stream_read, stream_write, stream_seek, stream_tell };

// Edits the plugin reports (from its GUI or automation): logged
std::atomic<int> g_edits_logged{ 0 };

tresult RV_PLUGIN_API handler_qi(
	IComponentHandler* self, const char* iid, void** obj) {
	return query(self, iid, IID_IComponentHandler, obj);
}

tresult RV_PLUGIN_API handler_edit(IComponentHandler*, ParamID) {
	return kResultOk;
}

tresult RV_PLUGIN_API handler_perform(
	IComponentHandler*, ParamID id, ParamValue v) {
	if (g_edits_logged++ < 5)
		rv_host_log("performEdit(#%u, %.4f) (logged the first 5 times)", id, v);
	return kResultOk;
}

tresult RV_PLUGIN_API handler_restart(IComponentHandler*, int32 flags) {
	rv_host_log("restartComponent(flags=%x)", (unsigned)flags);
	return kResultOk;
}

IComponentHandler_vtbl g_handler_vtbl = { handler_qi, no_ref<IComponentHandler>,
	no_ref<IComponentHandler>, handler_edit, handler_perform, handler_edit,
	handler_restart };
IComponentHandler g_handler = { &g_handler_vtbl };

tresult RV_PLUGIN_API queue_qi(
	IParamValueQueue* self, const char* iid, void** obj) {
	return query(self, iid, IID_IParamValueQueue, obj);
}

ParamID RV_PLUGIN_API queue_id(IParamValueQueue* self) {
	return ((HostQueue*)self)->id;
}

int32 RV_PLUGIN_API queue_count(IParamValueQueue*) { return 1; }

tresult RV_PLUGIN_API queue_point(
	IParamValueQueue* self, int32 index, int32* offset, ParamValue* value) {

	if (index != 0) return kResultFalse;
	if (offset) *offset = 0;
	if (value) *value = ((HostQueue*)self)->value;
	return kResultOk;
}

tresult RV_PLUGIN_API queue_add(IParamValueQueue*, int32, ParamValue, int32*) {
	return kResultFalse; // input changes
}

IParamValueQueue_vtbl g_queue_vtbl = { queue_qi, no_ref<IParamValueQueue>,
	no_ref<IParamValueQueue>, queue_id, queue_count, queue_point, queue_add };

tresult RV_PLUGIN_API changes_qi(
	IParameterChanges* self, const char* iid, void** obj) {
	return query(self, iid, IID_IParameterChanges, obj);
}

int32 RV_PLUGIN_API changes_count(IParameterChanges* self) {
	return ((HostChanges*)self)->count;
}

IParamValueQueue* RV_PLUGIN_API changes_get(
	IParameterChanges* self, int32 index) {
	HostChanges* c = (HostChanges*)self;
	return index >= 0 && index < c->count ? &c->queues[index].base : nullptr;
}

IParamValueQueue* RV_PLUGIN_API changes_add(
	IParameterChanges*, const ParamID*, int32*) {
	return nullptr; // input changes
}

IParameterChanges_vtbl g_changes_vtbl = { changes_qi, no_ref<IParameterChanges>,
	no_ref<IParameterChanges>, changes_count, changes_get, changes_add };

// A String128 as UTF-8
std::string utf8(const TChar* s, size_t max) {
	std::string out;
	for (size_t i = 0; i < max && s[i]; i++) {
		const uint32_t c = s[i];
		if (c < 0x80) out += char(c);
		else if (c < 0x800) {
			out += char(0xC0 | (c >> 6));
			out += char(0x80 | (c & 0x3F));
		} else {
			out += char(0xE0 | (c >> 12));
			out += char(0x80 | ((c >> 6) & 0x3F));
			out += char(0x80 | (c & 0x3F));
		}
	}
	return out;
}

} // namespace rv::vst3::abi
