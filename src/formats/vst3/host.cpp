/**
 * @file host.cpp
 * @brief VST3 for reverb-minihost: Some of the interfaces it calls, declared as
 * raw vtables, and the IIDs of iids.inc.
 */
#include "formats/host.hpp"
#include <cstdint>
#include <cstring>
#include <string>

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
struct IComponent;
struct IComponent_vtbl {
	RV_FUNKNOWN_METHODS(IComponent)
	tresult(RV_PLUGIN_API* initialize)(IComponent* self, FUnknown* context);
	tresult(RV_PLUGIN_API* terminate)(IComponent* self);
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

#undef RV_FUNKNOWN_METHODS

constexpr tresult kResultOk = 0;
#ifdef _WIN32 // E_NOINTERFACE
constexpr tresult kNoInterface = (tresult)0x80004002;
#else
constexpr tresult kNoInterface = -1;
#endif

/// DECLARE_CLASS_IID words as TUID bytes, laid out like INLINE_UID
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
		rv_host_log("VST3 \"%s\": factory %p, component %p", name_.c_str(),
			(void*)factory_, (void*)component_);
	}

	void unload() override {
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
	IPluginFactory* factory_ = nullptr;
	IComponent* component_ = nullptr;
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
