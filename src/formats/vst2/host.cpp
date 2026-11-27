/**
 * @file host.cpp
 * @brief VST2 for reverb-minihost. Declares only the part of the ABI it uses
 * to remove dependence on the IDB TIL
 */
#include "formats/host.hpp"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>

namespace rv::vst2 {
namespace {

typedef int32_t VstInt32;
typedef intptr_t VstIntPtr;

struct AEffect;
typedef VstIntPtr (*audioMasterCallback)(AEffect* effect, VstInt32 opcode,
	VstInt32 index, VstIntPtr value, void* ptr, float opt);
typedef VstIntPtr (*AEffectDispatcherProc)(AEffect* effect, VstInt32 opcode,
	VstInt32 index, VstIntPtr value, void* ptr, float opt);
typedef void (*AEffectProcessProc)(
	AEffect* effect, float** inputs, float** outputs, VstInt32 frames);

struct AEffect {
	VstInt32 magic;
	AEffectDispatcherProc dispatcher;
	AEffectProcessProc process; ///< Accumulating, deprecated
	void* setParameter;
	void* getParameter;
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
	effMainsChanged = 12,
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

// main is last: plenty of non-VST modules export it too
const char* const kEntrySymbols[] = { "VSTPluginMain", "main_macho", "main",
	nullptr };

// What the host can do, for audioMasterCanDo
const char* const kHostCanDo[] = { "sendVstEvents", "sendVstMidiEvent",
	"sendVstTimeInfo", "receiveVstEvents", "receiveVstMidiEvent", nullptr };

// Host state the callback answers from: one plugin per process
struct HostState {
	host::ProcessSetup setup;
	VstTimeInfo time = {};
} g_state;

// Some opcodes come with every block: log each only its first few times
bool should_log(VstInt32 opcode) {
	static std::atomic<int> counts[64];
	if (opcode < 0 || opcode >= 64) return true;
	const int n = counts[opcode]++;
	if (n == 3)
		rv_host_log("audioMaster(opcode=%d): further calls not logged", opcode);
	return n < 3;
}

VstIntPtr host_callback(AEffect*, VstInt32 opcode, VstInt32 index,
	VstIntPtr value, void* ptr, float) {

	if (should_log(opcode)) {
		if (opcode == audioMasterCanDo && ptr != nullptr)
			rv_host_log("audioMaster(canDo \"%s\")", (const char*)ptr);
		else
			rv_host_log("audioMaster(opcode=%d, index=%d, value=%ld)", opcode,
				index, (long)value);
	}

	switch (opcode) {
	case audioMasterVersion:	   return 2400;
	case audioMasterWantMidi:	   return 1;
	case audioMasterGetTime:	   return (VstIntPtr)&g_state.time;
	case audioMasterGetSampleRate: return (VstIntPtr)g_state.setup.sample_rate;
	case audioMasterGetBlockSize:  return (VstIntPtr)g_state.setup.block_size;
	case audioMasterGetCurrentProcessLevel:
		return host::on_audio_thread() ? kVstProcessLevelRealtime
									   : kVstProcessLevelUser;

	case audioMasterGetVendorString:
		if (ptr) std::strcpy((char*)ptr, "REverb");
		return 1;

	case audioMasterGetProductString:
		if (ptr) std::strcpy((char*)ptr, "reverb-minihost");
		return 1;

	case audioMasterGetVendorVersion: return 1000;

	case audioMasterCanDo:
		for (const char* const* c = kHostCanDo; ptr && *c; c++)
			if (!std::strcmp(*c, (const char*)ptr)) return 1;
		return 0;

	default: return 0; // "not supported"
	}
}

class Vst2Host final : public host::PluginHost {
public:
	Interface format() const override { return Interface::VST2; }

	bool load(host::SharedLib& lib, const host::LoadOptions&) override {
		VstEntry entry = nullptr;
		for (const char* const* n = kEntrySymbols; *n; n++) {
			if (void* s = lib.sym(*n)) {
				rv_host_log("entry point: %s @ %p", *n, s);
				entry = (VstEntry)s;
				break;
			}
		}
		if (entry == nullptr) {
			rv_host_log("no VST2 entry point exported");
			return false;
		}

		host::step("entry");
		effect_ = entry(host_callback);
		if (effect_ == nullptr || effect_->magic != kEffectMagic) {
			rv_host_log(
				"entry returned %p, not a valid AEffect", (void*)effect_);
			effect_ = nullptr;
			return false;
		}
		rv_host_log("AEffect @ %p: uniqueID=%08x, %d in / %d out, %d params, "
					"flags=%x",
			(void*)effect_, (unsigned)effect_->uniqueID, effect_->numInputs,
			effect_->numOutputs, effect_->numParams, (unsigned)effect_->flags);

		host::step("init");
		dispatch(effOpen);
		return true;
	}

	void describe() const override {
		if (effect_ == nullptr) return;
		char name[256] = {}, vendor[256] = {};
		dispatch(effGetEffectName, 0, 0, name);
		dispatch(effGetVendorString, 0, 0, vendor);
		rv_host_log("effect \"%s\" by \"%s\", dispatcher @ %p", name, vendor,
			(void*)effect_->dispatcher);
	}

	bool activate(const host::ProcessSetup& s) override {
		if (effect_ == nullptr) return false;
		replacing_ = (effect_->flags & effFlagsCanReplacing) &&
					 effect_->processReplacing != nullptr;
		if (!replacing_ && effect_->process == nullptr) {
			rv_host_log("the AEffect has no process callback");
			return false;
		}

		g_state.setup = s;
		in_.resize(uint32_t(std::max(effect_->numInputs, 0)), s.block_size);
		out_.resize(uint32_t(std::max(effect_->numOutputs, 0)), s.block_size);
		for (int i = 0; i < kMaxEvents; i++)
			events_.events[i] = &midi_[i];

		char can[] = "receiveVstMidiEvent";
		midi_in_ = (effect_->flags & effFlagsIsSynth) ||
				   dispatch(effCanDo, 0, 0, can) > 0;

		dispatch(effSetSampleRate, 0, 0, nullptr, float(s.sample_rate));
		dispatch(effSetBlockSize, 0, VstIntPtr(s.block_size));
		dispatch(effMainsChanged, 0, 1);
		rv_host_log("%s, %s%s", replacing_ ? "processReplacing" : "process",
			midi_in_ ? "takes MIDI" : "no MIDI input",
			(effect_->flags & effFlagsIsSynth) ? " (synth)" : "");
		return true;
	}

	bool start_processing() override {
		dispatch(effStartProcess);
		return true;
	}

	float process(const host::Block& b) override {
		update_time(b);
		in_.fill(b.input, b.frames);
		out_.clear(b.frames);

		if (midi_in_ && b.num_notes > 0) {
			events_.numEvents = 0;
			for (size_t i = 0; i < b.num_notes && i < kMaxEvents; i++) {
				const host::NoteEvent& n = b.notes[i];
				VstMidiEvent& e = midi_[events_.numEvents++];
				e = {};
				e.type = kVstMidiType;
				e.byteSize = sizeof(VstMidiEvent);
				e.deltaFrames = VstInt32(n.offset);
				e.midiData[0] = char(n.on ? 0x90 : 0x80);
				e.midiData[1] = char(n.key);
				e.midiData[2] = char(n.on ? int(n.velocity * 127.f) : 0);
			}
			dispatch(effProcessEvents, 0, 0, &events_);
		}

		if (replacing_)
			effect_->processReplacing(
				effect_, in_.ptrs(), out_.ptrs(), VstInt32(b.frames));
		else
			effect_->process(
				effect_, in_.ptrs(), out_.ptrs(), VstInt32(b.frames));
		return out_.peak(b.frames);
	}

	void stop_processing() override { dispatch(effStopProcess); }

	void deactivate() override { dispatch(effMainsChanged, 0, 0); }

	void unload() override {
		if (effect_ != nullptr) dispatch(effClose);
		effect_ = nullptr;
	}

private:
	VstIntPtr dispatch(VstInt32 op, VstInt32 index = 0, VstIntPtr value = 0,
		void* ptr = nullptr, float opt = 0.f) const {
		return effect_->dispatcher(effect_, op, index, value, ptr, opt);
	}

	// The time info audioMasterGetTime returns while the block runs
	static void update_time(const host::Block& b) {
		VstTimeInfo& t = g_state.time;
		const double ppq = b.ppq();
		t = {};
		t.samplePos = double(b.sample_pos);
		t.sampleRate = b.sample_rate;
		t.ppqPos = ppq;
		t.tempo = b.tempo;
		t.barStartPos = std::floor(ppq / 4.0) * 4.0;
		t.timeSigNumerator = 4;
		t.timeSigDenominator = 4;
		t.flags = kVstTransportPlaying | kVstPpqPosValid | kVstTempoValid |
				  kVstBarsValid | kVstTimeSigValid;
	}

	AEffect* effect_ = nullptr;
	bool replacing_ = false;
	bool midi_in_ = false;
	host::Channels in_, out_;
	VstMidiEvent midi_[kMaxEvents] = {};
	VstEventsN events_ = {};
};

std::unique_ptr<host::PluginHost> create() {
	return std::make_unique<Vst2Host>();
}

} // namespace

const HostModule host_module = { Interface::VST2, &create };

} // namespace rv::vst2
