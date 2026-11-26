/**
 * @file host.cpp
 * @brief VST2 for reverb-minihost. Declares only the part of the ABI it uses
 * to remove dependence on the IDB TIL
 */
#include "formats/host.hpp"
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

struct AEffect {
	VstInt32 magic;
	AEffectDispatcherProc dispatcher;
	void* process;
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
};

enum : VstInt32 { // AEffectOpcodes
	effOpen = 0,
	effClose = 1,
	effGetEffectName = 45,
	effGetVendorString = 47,
};

enum : VstInt32 { // AudioMasterOpcodes
	audioMasterVersion = 1,
	audioMasterGetVendorString = 32,
	audioMasterGetProductString = 33,
	audioMasterGetVendorVersion = 34,
};

typedef AEffect* (*VstEntry)(audioMasterCallback);

constexpr VstInt32 kEffectMagic = 0x56737450; // 'VstP'

// main is last: plenty of non-VST modules export it too
const char* const kEntrySymbols[] = { "VSTPluginMain", "main_macho", "main",
	nullptr };

VstIntPtr host_callback(AEffect*, VstInt32 opcode, VstInt32 index,
	VstIntPtr value, void* ptr, float) {

	rv_host_log("audioMaster(opcode=%d, index=%d, value=%ld)", opcode, index,
		(long)value);

	switch (opcode) {
	case audioMasterVersion: return 2400;
	case audioMasterGetVendorString:
		if (ptr) std::strcpy((char*)ptr, "REverb");
		return 1;

	case audioMasterGetProductString:
		if (ptr) std::strcpy((char*)ptr, "reverb-minihost");
		return 1;

	case audioMasterGetVendorVersion: return 1000;

	default:						  return 0; // "not supported"
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

		effect_->dispatcher(effect_, effOpen, 0, 0, nullptr, 0.f);
		return true;
	}

	void describe() const override {
		if (effect_ == nullptr) return;
		char name[256] = {}, vendor[256] = {};
		effect_->dispatcher(effect_, effGetEffectName, 0, 0, name, 0.f);
		effect_->dispatcher(effect_, effGetVendorString, 0, 0, vendor, 0.f);
		rv_host_log("effect \"%s\" by \"%s\", dispatcher @ %p", name, vendor,
			(void*)effect_->dispatcher);
	}

	void unload() override {
		if (effect_ != nullptr)
			effect_->dispatcher(effect_, effClose, 0, 0, nullptr, 0.f);
		effect_ = nullptr;
	}

private:
	AEffect* effect_ = nullptr;
};

std::unique_ptr<host::PluginHost> create() {
	return std::make_unique<Vst2Host>();
}

} // namespace

const HostModule host_module = { Interface::VST2, &create };

} // namespace rv::vst2
