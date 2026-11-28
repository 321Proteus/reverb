#pragma once
/**
 * @file host_abi.hpp
 * @brief The part of the VST2 ABI reverb-minihost calls, declared by hand so
 * it depends on neither the SDK nor the IDB type libraries
 */
#include <cstdint>

namespace rv::vst2::abi {

typedef int32_t VstInt32;
typedef intptr_t VstIntPtr;

struct AEffect;
typedef VstIntPtr (*audioMasterCallback)(AEffect* effect, VstInt32 opcode,
	VstInt32 index, VstIntPtr value, void* ptr, float opt);
typedef VstIntPtr (*AEffectDispatcherProc)(AEffect* effect, VstInt32 opcode,
	VstInt32 index, VstIntPtr value, void* ptr, float opt);
typedef void (*AEffectProcessProc)(
	AEffect* effect, float** inputs, float** outputs, VstInt32 frames);
typedef void (*AEffectSetParameterProc)(
	AEffect* effect, VstInt32 index, float value);
typedef float (*AEffectGetParameterProc)(AEffect* effect, VstInt32 index);

struct AEffect {
	VstInt32 magic;
	AEffectDispatcherProc dispatcher;
	AEffectProcessProc process; ///< Accumulating, deprecated
	AEffectSetParameterProc setParameter;
	AEffectGetParameterProc getParameter;
	VstInt32 numPrograms;
	VstInt32 numParams;
	VstInt32 numInputs;
	VstInt32 numOutputs;
	VstInt32 flags;
	VstIntPtr resvd1;
	VstIntPtr resvd2;
	VstInt32 initialDelay;
	VstInt32 realQualities;
	VstInt32 offQualities;
	float ioRatio;
	void* object;
	void* user;
	VstInt32 uniqueID;
	VstInt32 version;
	AEffectProcessProc processReplacing;
	void* processDoubleReplacing;
};

struct VstTimeInfo {
	double samplePos;
	double sampleRate;
	double nanoSeconds;
	double ppqPos;
	double tempo;
	double barStartPos;
	double cycleStartPos;
	double cycleEndPos;
	VstInt32 timeSigNumerator;
	VstInt32 timeSigDenominator;
	VstInt32 smpteOffset;
	VstInt32 smpteFrameRate;
	VstInt32 samplesToNextClock;
	VstInt32 flags;
};

struct VstMidiEvent {
	VstInt32 type; ///< kVstMidiType
	VstInt32 byteSize;
	VstInt32 deltaFrames;
	VstInt32 flags;
	VstInt32 noteLength;
	VstInt32 noteOffset;
	char midiData[4];
	char detune;
	char noteOffVelocity;
	char reserved1;
	char reserved2;
};

constexpr int kMaxEvents = 16;

// VstEvents with room for kMaxEvents (the SDK declares events[2])
struct VstEventsN {
	VstInt32 numEvents;
	VstIntPtr reserved;
	VstMidiEvent* events[kMaxEvents];
};

enum : VstInt32 { // AEffectOpcodes
	effOpen = 0,
	effClose = 1,
	effSetSampleRate = 10,
	effSetBlockSize = 11,
	effGetParamLabel = 6,
	effGetParamDisplay = 7,
	effGetParamName = 8,
	effMainsChanged = 12,
	effGetChunk = 23,
	effSetChunk = 24,
	effProcessEvents = 25,
	effGetEffectName = 45,
	effGetVendorString = 47,
	effCanDo = 51,
	effStartProcess = 71,
	effStopProcess = 72,
};

enum : VstInt32 { // AudioMasterOpcodes
	audioMasterVersion = 1,
	audioMasterWantMidi = 6,
	audioMasterGetTime = 7,
	audioMasterGetSampleRate = 16,
	audioMasterGetBlockSize = 17,
	audioMasterGetCurrentProcessLevel = 23,
	audioMasterGetVendorString = 32,
	audioMasterGetProductString = 33,
	audioMasterGetVendorVersion = 34,
	audioMasterCanDo = 37,
};

enum : VstInt32 {
	effFlagsCanReplacing = 16,
	effFlagsProgramChunks = 32,
	effFlagsIsSynth = 256,
	kVstMidiType = 1,
	kVstTransportPlaying = 2,
	kVstPpqPosValid = 512,
	kVstTempoValid = 1024,
	kVstBarsValid = 2048,
	kVstTimeSigValid = 8192,
	kVstProcessLevelUser = 1,
	kVstProcessLevelRealtime = 2,
};

typedef AEffect* (*VstEntry)(audioMasterCallback);

constexpr VstInt32 kEffectMagic = 0x56737450; // 'VstP'

} // namespace rv::vst2::abi
