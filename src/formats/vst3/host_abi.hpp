#pragma once
/**
 * @file host_abi.hpp
 * @brief The part of the VST3 ABI reverb-minihost calls, declared as raw
 * vtables, and the IIDs of iids.inc
 */
#include <cstdint>
#include <cstring>

namespace rv::vst3::abi {

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
	void* inputParameterChanges; // IParameterChanges
	void* outputParameterChanges; // none: what the plugin reports is dropped
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

// Parameters and state

typedef uint32 ParamID;
typedef double ParamValue;
typedef int64_t int64;

struct ParameterInfo {
	ParamID id;
	TChar title[128];
	TChar shortTitle[128];
	TChar units[128];
	int32 stepCount; ///< 0: continuous
	ParamValue defaultNormalizedValue;
	int32 unitId;
	int32 flags;
};
static_assert(sizeof(ParameterInfo) == 792, "ParameterInfo layout");

struct IBStream;
struct IBStream_vtbl {
	RV_FUNKNOWN_METHODS(IBStream)
	tresult(RV_PLUGIN_API* read)(
		IBStream* self, void* buffer, int32 numBytes, int32* numBytesRead);
	tresult(RV_PLUGIN_API* write)(
		IBStream* self, void* buffer, int32 numBytes, int32* numBytesWritten);
	tresult(RV_PLUGIN_API* seek)(
		IBStream* self, int64 pos, int32 mode, int64* result);
	tresult(RV_PLUGIN_API* tell)(IBStream* self, int64* pos);
};
struct IBStream {
	IBStream_vtbl* __vftable;
};

struct IComponentHandler;
struct IComponentHandler_vtbl {
	RV_FUNKNOWN_METHODS(IComponentHandler)
	tresult(RV_PLUGIN_API* beginEdit)(IComponentHandler* self, ParamID id);
	tresult(RV_PLUGIN_API* performEdit)(
		IComponentHandler* self, ParamID id, ParamValue value);
	tresult(RV_PLUGIN_API* endEdit)(IComponentHandler* self, ParamID id);
	tresult(RV_PLUGIN_API* restartComponent)(
		IComponentHandler* self, int32 flags);
};
struct IComponentHandler {
	IComponentHandler_vtbl* __vftable;
};

// IPluginBase + IEditController
struct IEditController;
struct IEditController_vtbl {
	RV_FUNKNOWN_METHODS(IEditController)
	tresult(RV_PLUGIN_API* initialize)(IEditController* self, FUnknown* ctx);
	tresult(RV_PLUGIN_API* terminate)(IEditController* self);
	tresult(RV_PLUGIN_API* setComponentState)(
		IEditController* self, IBStream* state);
	tresult(RV_PLUGIN_API* setState)(IEditController* self, IBStream* state);
	tresult(RV_PLUGIN_API* getState)(IEditController* self, IBStream* state);
	int32(RV_PLUGIN_API* getParameterCount)(IEditController* self);
	tresult(RV_PLUGIN_API* getParameterInfo)(
		IEditController* self, int32 index, ParameterInfo* info);
	tresult(RV_PLUGIN_API* getParamStringByValue)(
		IEditController* self, ParamID id, ParamValue value, TChar* string);
	tresult(RV_PLUGIN_API* getParamValueByString)(
		IEditController* self, ParamID id, TChar* string, ParamValue* value);
	ParamValue(RV_PLUGIN_API* normalizedParamToPlain)(
		IEditController* self, ParamID id, ParamValue value);
	ParamValue(RV_PLUGIN_API* plainParamToNormalized)(
		IEditController* self, ParamID id, ParamValue value);
	ParamValue(RV_PLUGIN_API* getParamNormalized)(
		IEditController* self, ParamID id);
	tresult(RV_PLUGIN_API* setParamNormalized)(
		IEditController* self, ParamID id, ParamValue value);
	tresult(RV_PLUGIN_API* setComponentHandler)(
		IEditController* self, IComponentHandler* handler);
	void*(RV_PLUGIN_API* createView)(IEditController* self, const char* name);
};
struct IEditController {
	IEditController_vtbl* __vftable;
};

struct IConnectionPoint;
struct IConnectionPoint_vtbl {
	RV_FUNKNOWN_METHODS(IConnectionPoint)
	tresult(RV_PLUGIN_API* connect)(
		IConnectionPoint* self, IConnectionPoint* other);
	tresult(RV_PLUGIN_API* disconnect)(
		IConnectionPoint* self, IConnectionPoint* other);
	tresult(RV_PLUGIN_API* notify)(IConnectionPoint* self, void* message);
};
struct IConnectionPoint {
	IConnectionPoint_vtbl* __vftable;
};

struct IParamValueQueue;
struct IParamValueQueue_vtbl {
	RV_FUNKNOWN_METHODS(IParamValueQueue)
	ParamID(RV_PLUGIN_API* getParameterId)(IParamValueQueue* self);
	int32(RV_PLUGIN_API* getPointCount)(IParamValueQueue* self);
	tresult(RV_PLUGIN_API* getPoint)(IParamValueQueue* self, int32 index,
		int32* sampleOffset, ParamValue* value);
	tresult(RV_PLUGIN_API* addPoint)(IParamValueQueue* self,
		int32 sampleOffset, ParamValue value, int32* index);
};
struct IParamValueQueue {
	IParamValueQueue_vtbl* __vftable;
};

struct IParameterChanges;
struct IParameterChanges_vtbl {
	RV_FUNKNOWN_METHODS(IParameterChanges)
	int32(RV_PLUGIN_API* getParameterCount)(IParameterChanges* self);
	IParamValueQueue*(RV_PLUGIN_API* getParameterData)(
		IParameterChanges* self, int32 index);
	IParamValueQueue*(RV_PLUGIN_API* addParameterData)(
		IParameterChanges* self, const ParamID* id, int32* index);
};
struct IParameterChanges {
	IParameterChanges_vtbl* __vftable;
};

#undef RV_FUNKNOWN_METHODS

constexpr tresult kResultOk = 0;
constexpr tresult kResultFalse = 1;
constexpr tresult kNotImplemented = 3;
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

} // namespace rv::vst3::abi
