/**
 * @file host.cpp
 * @brief VST3 for reverb-minihost: Some of the interfaces it calls, declared as
 * raw vtables, and the IIDs of iids.inc.
 */
#include "formats/host.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace rv::vst3 {
namespace {

// PLUGIN_API: __stdcall on 32-bit Windows
#if defined(_WIN32) && !defined(_WIN64)
#define RV_PLUGIN_API __stdcall
#else
#define RV_PLUGIN_API
#endif

typedef int32_t int32;
typedef uint32_t uint32;
typedef int32 tresult;
typedef char16_t TChar;

#define RV_FUNKNOWN_METHODS(Self)                                              \
	tresult(RV_PLUGIN_API* queryInterface)(                                    \
		Self * self, const char* iid, void** obj);                             \
	uint32(RV_PLUGIN_API* addRef)(Self * self);                                \
	uint32(RV_PLUGIN_API* release)(Self * self);

struct FUnknown;
struct FUnknown_vtbl {
	RV_FUNKNOWN_METHODS(FUnknown)
};
struct FUnknown {
	FUnknown_vtbl* __vftable;
};

struct PFactoryInfo {
	char vendor[64];
	char url[256];
	char email[128];
	int32 flags;
};
struct PClassInfo {
	char cid[16];
	int32 cardinality;
	char category[32];
	char name[64];
};

struct IPluginFactory;
struct IPluginFactory_vtbl {
	RV_FUNKNOWN_METHODS(IPluginFactory)
	tresult(RV_PLUGIN_API* getFactoryInfo)(
		IPluginFactory* self, PFactoryInfo* info);
	int32(RV_PLUGIN_API* countClasses)(IPluginFactory* self);
	tresult(RV_PLUGIN_API* getClassInfo)(
		IPluginFactory* self, int32 index, PClassInfo* info);
	tresult(RV_PLUGIN_API* createInstance)(
		IPluginFactory* self, const char* cid, const char* iid, void** obj);
};
struct IPluginFactory {
	IPluginFactory_vtbl* __vftable;
};

// IPluginBase + IComponent, up to the methods we call
typedef uint8_t TBool;
typedef int32 MediaType;	// kAudio, kEvent
typedef int32 BusDirection; // kInput, kOutput
enum : int32 { kAudio = 0, kEvent = 1, kInput = 0, kOutput = 1 };

struct BusInfo {
	MediaType mediaType;
	BusDirection direction;
	int32 channelCount;
	TChar name[128];
	int32 busType;
	uint32 flags;
};

// IPluginBase + IComponent
struct IComponent;
struct IComponent_vtbl {
	RV_FUNKNOWN_METHODS(IComponent)
	tresult(RV_PLUGIN_API* initialize)(IComponent* self, FUnknown* context);
	tresult(RV_PLUGIN_API* terminate)(IComponent* self);
	tresult(RV_PLUGIN_API* getControllerClassId)(IComponent* self, char* cid);
	tresult(RV_PLUGIN_API* setIoMode)(IComponent* self, int32 mode);
	int32(RV_PLUGIN_API* getBusCount)(
		IComponent* self, MediaType type, BusDirection dir);
	tresult(RV_PLUGIN_API* getBusInfo)(IComponent* self, MediaType type,
		BusDirection dir, int32 index, BusInfo* bus);
	tresult(RV_PLUGIN_API* getRoutingInfo)(
		IComponent* self, void* in, void* out);
	tresult(RV_PLUGIN_API* activateBus)(IComponent* self, MediaType type,
		BusDirection dir, int32 index, TBool state);
	tresult(RV_PLUGIN_API* setActive)(IComponent* self, TBool state);
	tresult(RV_PLUGIN_API* setState)(IComponent* self, void* state);
	tresult(RV_PLUGIN_API* getState)(IComponent* self, void* state);
};
struct IComponent {
	IComponent_vtbl* __vftable;
};

struct IHostApplication;
struct IHostApplication_vtbl {
	RV_FUNKNOWN_METHODS(IHostApplication)
	tresult(RV_PLUGIN_API* getName)(IHostApplication* self, TChar* name);
	tresult(RV_PLUGIN_API* createInstance)(
		IHostApplication* self, char* cid, char* iid, void** obj);
};
struct IHostApplication {
	IHostApplication_vtbl* __vftable;
};

// Processing

struct ProcessSetup {
	int32 processMode;		  // kRealtime
	int32 symbolicSampleSize; // kSample32
	int32 maxSamplesPerBlock;
	double sampleRate;
};

struct AudioBusBuffers {
	int32 numChannels;
	uint64_t silenceFlags;
	float** channelBuffers32;
};

struct ProcessContext {
	uint32 state;
	double sampleRate;
	int64_t projectTimeSamples;
	int64_t systemTime;
	int64_t continousTimeSamples;
	double projectTimeMusic;
	double barPositionMusic;
	double cycleStartMusic;
	double cycleEndMusic;
	double tempo;
	int32 timeSigNumerator;
	int32 timeSigDenominator;
	struct {
		uint8_t keyNote;
		uint8_t rootNote;
		int16_t chordMask;
	} chord;
	int32 smpteOffsetSubframes;
	struct {
		uint32 framesPerSecond;
		uint32 flags;
	} frameRate;
	int32 samplesToNextClock;
};

enum : uint32 { // ProcessContext::StatesAndFlags
	kPlaying = 2,
	kProjectTimeMusicValid = 512,
	kTempoValid = 1024,
	kBarPositionValid = 2048,
	kTimeSigValid = 8192,
	kContTimeValid = 131072,
};

struct NoteOnEvent {
	int16_t channel;
	int16_t pitch;
	float tuning;
	float velocity;
	int32 length;
	int32 noteId;
};
struct NoteOffEvent {
	int16_t channel;
	int16_t pitch;
	float velocity;
	int32 noteId;
	float tuning;
};

struct Event {
	int32 busIndex;
	int32 sampleOffset;
	double ppqPosition;
	uint16_t flags;
	uint16_t type; // kNoteOnEvent, kNoteOffEvent
	union {
		NoteOnEvent noteOn;
		NoteOffEvent noteOff;
		// The largest member, NoteExpressionTextEvent: sets size and alignment
		struct {
			uint32 typeId;
			int32 noteId;
			uint32 textLen;
			const TChar* text;
		} largest_;
	};
};
static_assert(sizeof(void*) != 8 || sizeof(Event) == 48, "Event layout");
enum : uint16_t { kNoteOnEvent = 0, kNoteOffEvent = 1 };

struct IEventList;
struct IEventList_vtbl {
	RV_FUNKNOWN_METHODS(IEventList)
	int32(RV_PLUGIN_API* getEventCount)(IEventList* self);
	tresult(RV_PLUGIN_API* getEvent)(IEventList* self, int32 index, Event* e);
	tresult(RV_PLUGIN_API* addEvent)(IEventList* self, Event* e);
};
struct IEventList {
	IEventList_vtbl* __vftable;
};

struct ProcessData {
	int32 processMode;
	int32 symbolicSampleSize;
	int32 numSamples;
	int32 numInputs;
	int32 numOutputs;
	AudioBusBuffers* inputs;
	AudioBusBuffers* outputs;
	void* inputParameterChanges; // IParameterChanges, none sent
	void* outputParameterChanges;
	IEventList* inputEvents;
	IEventList* outputEvents;
	ProcessContext* processContext;
};

struct IAudioProcessor;
struct IAudioProcessor_vtbl {
	RV_FUNKNOWN_METHODS(IAudioProcessor)
	tresult(RV_PLUGIN_API* setBusArrangements)(IAudioProcessor* self,
		uint64_t* inputs, int32 numIns, uint64_t* outputs, int32 numOuts);
	tresult(RV_PLUGIN_API* getBusArrangement)(
		IAudioProcessor* self, BusDirection dir, int32 index, uint64_t* arr);
	tresult(RV_PLUGIN_API* canProcessSampleSize)(
		IAudioProcessor* self, int32 symbolicSampleSize);
	uint32(RV_PLUGIN_API* getLatencySamples)(IAudioProcessor* self);
	tresult(RV_PLUGIN_API* setupProcessing)(
		IAudioProcessor* self, ProcessSetup* setup);
	tresult(RV_PLUGIN_API* setProcessing)(IAudioProcessor* self, TBool state);
	tresult(RV_PLUGIN_API* process)(IAudioProcessor* self, ProcessData* data);
	uint32(RV_PLUGIN_API* getTailSamples)(IAudioProcessor* self);
};
struct IAudioProcessor {
	IAudioProcessor_vtbl* __vftable;
};

#undef RV_FUNKNOWN_METHODS

constexpr tresult kResultOk = 0;
constexpr tresult kResultFalse = 1;
#ifdef _WIN32 // E_NOINTERFACE
constexpr tresult kNoInterface = (tresult)0x80004002;
#else
constexpr tresult kNoInterface = -1;
#endif

// DECLARE_CLASS_IID words as TUID bytes, laid out like INLINE_UID
struct Iid {
	char b[16];
	constexpr Iid(uint32_t l1, uint32_t l2, uint32_t l3, uint32_t l4) : b{} {
#ifdef _WIN32 // COM_COMPATIBLE
		b[0] = char(l1), b[1] = char(l1 >> 8), b[2] = char(l1 >> 16);
		b[3] = char(l1 >> 24), b[4] = char(l2 >> 16), b[5] = char(l2 >> 24);
		b[6] = char(l2), b[7] = char(l2 >> 8);
#else
		b[0] = char(l1 >> 24), b[1] = char(l1 >> 16), b[2] = char(l1 >> 8);
		b[3] = char(l1), b[4] = char(l2 >> 24), b[5] = char(l2 >> 16);
		b[6] = char(l2 >> 8), b[7] = char(l2);
#endif
		b[8] = char(l3 >> 24), b[9] = char(l3 >> 16), b[10] = char(l3 >> 8);
		b[11] = char(l3), b[12] = char(l4 >> 24), b[13] = char(l4 >> 16);
		b[14] = char(l4 >> 8), b[15] = char(l4);
	}
	bool operator==(const char* tuid) const {
		return std::memcmp(b, tuid, 16) == 0;
	}
};

#define RV_IID(Name, l1, l2, l3, l4) constexpr Iid IID_##Name(l1, l2, l3, l4);
#include "iids.inc"
#undef RV_IID

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

// The input IEventList: the block's notes, owned by the host
struct HostEventList {
	IEventList base;
	Event events[16];
	int32 count;
};

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

typedef IPluginFactory* (*GetFactoryFn)();
typedef bool (*ModuleEntryFn)(void*);
typedef bool (*ModuleExitFn)();

class Vst3Host final : public host::PluginHost {
public:
	Interface format() const override { return Interface::VST3; }

	bool load(host::SharedLib& lib, const host::LoadOptions& opts) override {
		auto get_factory = (GetFactoryFn)lib.sym("GetPluginFactory");
		if (get_factory == nullptr) {
			rv_host_log("GetPluginFactory is not exported");
			return false;
		}

		// The platform's module init hook, when the module has one
#if defined(_WIN32)
		const char *init_n = "InitDll", *exit_n = "ExitDll";
#elif defined(__APPLE__)
		const char *init_n = "bundleEntry", *exit_n = "bundleExit";
#else
		const char *init_n = "ModuleEntry", *exit_n = "ModuleExit";
#endif
		if (void* init = lib.sym(init_n)) {
			bundle_ = host::create_bundle_ref(opts.bundle_path);
			rv_host_log("calling %s @ %p", init_n, init);
#if defined(_WIN32)
			((bool (*)())init)();
#elif defined(__APPLE__)
			if (bundle_ == nullptr)
				rv_host_log("%s is not a bundle, bundleEntry gets null",
					opts.bundle_path.c_str());
			((ModuleEntryFn)init)(bundle_);
#else
			((ModuleEntryFn)init)(lib.handle()); // ModuleEntry takes the handle
#endif
			exit_ = (ModuleExitFn)lib.sym(exit_n);
			entered_ = true;
		}

		rv_host_log("entry point: GetPluginFactory @ %p", (void*)get_factory);
		factory_ = get_factory();

		if (factory_ == nullptr) {
			rv_host_log("GetPluginFactory returned null");
			return false;
		}

		IPluginFactory_vtbl* fv = factory_->__vftable;

		PFactoryInfo fi = {};
		if (fv->getFactoryInfo(factory_, &fi) == kResultOk)
			rv_host_log("factory: vendor=\"%s\" url=\"%s\"", fi.vendor, fi.url);

		int32 n = fv->countClasses(factory_);
		int32 audio_seen = 0;
		bool found = false;

		for (int32 i = 0; i < n; ++i) {
			PClassInfo ci = {};
			if (fv->getClassInfo(factory_, i, &ci) != kResultOk) continue;

			rv_host_log("  class %d: \"%s\" [%s]", i, ci.name, ci.category);

			if (!found && !std::strcmp(ci.category, "Audio Module Class") &&
				audio_seen++ == opts.class_index) {
				found = true;
				std::memcpy(cid_, ci.cid, 16);
				name_ = ci.name;
			}
		}

		if (!found) {
			rv_host_log("no audio module class #%d", opts.class_index);
			return false;
		}

		void* obj = nullptr;
		tresult r = fv->createInstance(factory_, cid_, IID_IComponent.b, &obj);
		if (r != kResultOk || obj == nullptr) {
			rv_host_log("createInstance failed (%d)", r);
			return false;
		}

		component_ = (IComponent*)obj;
		rv_host_log(
			"IComponent @ %p, vtable @ %p", obj, (void*)component_->__vftable);

		r = component_->__vftable->initialize(
			component_, (FUnknown*)&g_host_app);
		rv_host_log("IComponent::initialize -> %d", r);

		return r == kResultOk;
	}

	void describe() const override {
		rv_host_log("VST3 \"%s\": factory %p, component %p, processor %p",
			name_.c_str(), (void*)factory_, (void*)component_,
			(void*)processor_);
	}

	bool activate(const host::ProcessSetup& s) override {
		if (component_ == nullptr) return false;
		IComponent_vtbl* cv = component_->__vftable;

		void* obj = nullptr;
		if (cv->queryInterface(component_, IID_IAudioProcessor.b, &obj) !=
				kResultOk ||
			obj == nullptr) {
			rv_host_log("the component is not an IAudioProcessor");
			return false;
		}
		processor_ = (IAudioProcessor*)obj;
		IAudioProcessor_vtbl* pv = processor_->__vftable;
		rv_host_log("IAudioProcessor @ %p, vtable @ %p", obj, (void*)pv);

		if (pv->canProcessSampleSize(processor_, 0) != kResultOk) {
			rv_host_log("32-bit float processing is not supported");
			return false;
		}

		// Every audio bus gets buffers; the main ones (index 0) are activated
		ins_ = buses(kInput, s.block_size);
		outs_ = buses(kOutput, s.block_size);
		events_in_ = cv->getBusCount(component_, kEvent, kInput) > 0;
		if (events_in_) cv->activateBus(component_, kEvent, kInput, 0, 1);

		ProcessSetup ps = { 0, 0, int32(s.block_size), s.sample_rate };
		tresult r = pv->setupProcessing(processor_, &ps);
		rv_host_log("setupProcessing -> %d", r);
		if (r != kResultOk) return false;

		r = cv->setActive(component_, 1);
		rv_host_log("setActive(true) -> %d", r);
		active_ = r == kResultOk;

		event_list_.base.__vftable = &g_events_vtbl;
		return active_;
	}

	bool start_processing() override {
		// kNotImplemented is fine: setProcessing is optional for the plugin
		const tresult r = processor_->__vftable->setProcessing(processor_, 1);
		rv_host_log("setProcessing(true) -> %d", r);
		return true;
	}

	float process(const host::Block& b) override {
		for (Bus& bus : ins_)
			bus.ch.fill(b.input, b.frames);
		for (Bus& bus : outs_)
			bus.ch.clear(b.frames);

		event_list_.count = 0;
		for (size_t i = 0; i < b.num_notes && i < 16; i++) {
			const host::NoteEvent& n = b.notes[i];
			Event& e = event_list_.events[event_list_.count++];
			e = {};
			e.sampleOffset = int32(n.offset);
			e.ppqPosition = b.ppq();
			if (n.on) {
				e.type = kNoteOnEvent;
				e.noteOn = { 0, n.key, 0.f, n.velocity, 0, -1 };
			} else {
				e.type = kNoteOffEvent;
				e.noteOff = { 0, n.key, 0.f, -1, 0.f };
			}
		}

		const double ppq = b.ppq();
		ProcessContext ctx = {};
		ctx.state = kPlaying | kProjectTimeMusicValid | kTempoValid |
					kBarPositionValid | kTimeSigValid | kContTimeValid;
		ctx.sampleRate = b.sample_rate;
		ctx.projectTimeSamples = b.sample_pos;
		ctx.continousTimeSamples = b.sample_pos;
		ctx.projectTimeMusic = ppq;
		ctx.barPositionMusic = std::floor(ppq / 4.0) * 4.0;
		ctx.tempo = b.tempo;
		ctx.timeSigNumerator = 4;
		ctx.timeSigDenominator = 4;

		ProcessData d = {};
		d.numSamples = int32(b.frames);
		d.numInputs = int32(in_bufs_.size());
		d.numOutputs = int32(out_bufs_.size());
		d.inputs = in_bufs_.empty() ? nullptr : in_bufs_.data();
		d.outputs = out_bufs_.empty() ? nullptr : out_bufs_.data();
		d.inputEvents = events_in_ ? &event_list_.base : nullptr;
		d.processContext = &ctx;

		const tresult r = processor_->__vftable->process(processor_, &d);
		if (r != kResultOk && !process_failed_) {
			rv_host_log("process -> %d (only the first failure is logged)", r);
			process_failed_ = true;
		}

		float peak = 0.f;
		for (const Bus& bus : outs_)
			peak = std::max(peak, bus.ch.peak(b.frames));
		return peak;
	}

	void stop_processing() override {
		processor_->__vftable->setProcessing(processor_, 0);
	}

	void deactivate() override {
		if (active_) component_->__vftable->setActive(component_, 0);
		active_ = false;
	}

	void unload() override {
		if (processor_ != nullptr) {
			processor_->__vftable->release(processor_);
			processor_ = nullptr;
		}

		if (component_ != nullptr) {
			component_->__vftable->terminate(component_);
			component_->__vftable->release(component_);
			component_ = nullptr;
		}

		if (factory_ != nullptr) {
			factory_->__vftable->release(factory_);
			factory_ = nullptr;
		}

		if (entered_ && exit_ != nullptr) exit_();
		entered_ = false;
		host::release_bundle_ref(bundle_);
		bundle_ = nullptr;
	}

private:
	struct Bus {
		host::Channels ch;
	};

	// Buffers for every audio bus in @p dir, activating the main one
	std::vector<Bus> buses(BusDirection dir, uint32 frames) {
		IComponent_vtbl* cv = component_->__vftable;
		const int32 n = cv->getBusCount(component_, kAudio, dir);
		std::vector<Bus> out(size_t(n > 0 ? n : 0));
		std::vector<AudioBusBuffers>& bufs =
			dir == kInput ? in_bufs_ : out_bufs_;
		bufs.assign(out.size(), AudioBusBuffers{});

		for (int32 i = 0; i < n; i++) {
			BusInfo info = {};
			cv->getBusInfo(component_, kAudio, dir, i, &info);
			const int32 ch = info.channelCount > 0 ? info.channelCount : 0;
			out[i].ch.resize(uint32(ch), frames);
			if (i == 0) cv->activateBus(component_, kAudio, dir, 0, 1);
			rv_host_log("  audio %s bus %d: %d channels%s",
				dir == kInput ? "input" : "output", i, ch,
				i == 0 ? " (activated)" : "");
		}
		// The pointers after every resize, as the vector may move them
		for (size_t i = 0; i < out.size(); i++)
			bufs[i] = { int32(out[i].ch.count()), 0, out[i].ch.ptrs() };
		return out;
	}

	IPluginFactory* factory_ = nullptr;
	IComponent* component_ = nullptr;
	IAudioProcessor* processor_ = nullptr;
	std::vector<Bus> ins_, outs_;
	std::vector<AudioBusBuffers> in_bufs_, out_bufs_;
	HostEventList event_list_ = {};
	bool events_in_ = false;
	bool active_ = false;
	bool process_failed_ = false;
	void* bundle_ = nullptr;
	ModuleExitFn exit_ = nullptr;
	bool entered_ = false;
	char cid_[16] = {};
	std::string name_;
};

std::unique_ptr<host::PluginHost> create() {
	return std::make_unique<Vst3Host>();
}

} // namespace

const HostModule host_module = { Interface::VST3, &create };

} // namespace rv::vst3
