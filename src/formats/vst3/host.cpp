/**
 * @file host.cpp
 * @brief VST3 for reverb-minihost: the module entry, the component and its
 * controller, and the plugin's lifecycle (the ABI is in host_abi.hpp, the
 * objects the host implements in host_objects.hpp)
 */
#include "formats/host.hpp"
#include "host_abi.hpp"
#include "host_objects.hpp"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace rv::vst3 {
namespace {

using namespace abi;

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
		host::step("entry");
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

		host::step("create");
		void* obj = nullptr;
		tresult r = fv->createInstance(factory_, cid_, IID_IComponent.b, &obj);
		if (r != kResultOk || obj == nullptr) {
			rv_host_log("createInstance failed (%d)", r);
			return false;
		}

		component_ = (IComponent*)obj;
		rv_host_log(
			"IComponent @ %p, vtable @ %p", obj, (void*)component_->__vftable);

		host::step("init");
		r = component_->__vftable->initialize(
			component_, (FUnknown*)&g_host_app);
		rv_host_log("IComponent::initialize -> %d", r);
		if (r != kResultOk) return false;

		host::step("controller");
		create_controller();
		return true;
	}

	void describe() const override {
		rv_host_log("VST3 \"%s\": factory %p, component %p, processor %p, "
					"controller %p%s",
			name_.c_str(), (void*)factory_, (void*)component_,
			(void*)processor_, (void*)controller_,
			shared_controller_ ? " (the component itself)" : "");
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
		rv_host_log("  event input bus: %s",
			events_in_ ? "yes (notes are sent)" : "none (no notes)");

		ProcessSetup ps = { 0, 0, int32(s.block_size), s.sample_rate };
		tresult r = pv->setupProcessing(processor_, &ps);
		rv_host_log("setupProcessing -> %d", r);
		if (r != kResultOk) return false;

		r = cv->setActive(component_, 1);
		rv_host_log("setActive(true) -> %d", r);
		active_ = r == kResultOk;

		event_list_.base.__vftable = &g_events_vtbl;
		changes_.base.__vftable = &g_changes_vtbl;
		for (HostQueue& q : changes_.queues)
			q.base.__vftable = &g_queue_vtbl;
		return active_;
	}

	bool start_processing() override {
		// kNotImplemented is fine: setProcessing is optional for the plugin
		const tresult r = processor_->__vftable->setProcessing(processor_, 1);
		rv_host_log("setProcessing(true) -> %d%s", r,
			r == kNotImplemented ? " (kNotImplemented: optional, fine)" : "");
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

		// The queued parameter changes, at frame 0
		pending_.take(&taken_);
		changes_.count = 0;
		for (const host::ParamQueue::Change& c : taken_) {
			if (changes_.count == kMaxChanges) break;
			changes_.queues[changes_.count++] = { { &g_queue_vtbl }, c.id,
				c.value };
		}

		ProcessData d = {};
		d.numSamples = int32(b.frames);
		d.numInputs = int32(in_bufs_.size());
		d.numOutputs = int32(out_bufs_.size());
		d.inputs = in_bufs_.empty() ? nullptr : in_bufs_.data();
		d.outputs = out_bufs_.empty() ? nullptr : out_bufs_.data();
		d.inputEvents = events_in_ ? &event_list_.base : nullptr;
		d.inputParameterChanges = &changes_.base;
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

	size_t param_count() const override {
		if (controller_ == nullptr) return 0;
		const int32 n = controller_->__vftable->getParameterCount(controller_);
		return n > 0 ? size_t(n) : 0;
	}

	bool param(size_t index, host::Param* out) const override {
		if (controller_ == nullptr) return false;
		IEditController_vtbl* v = controller_->__vftable;
		ParameterInfo info = {};
		if (v->getParameterInfo(controller_, int32(index), &info) != kResultOk)
			return false;

		out->id = info.id;
		out->name = utf8(info.title);
		out->stepped = info.stepCount > 0;
		out->value = v->getParamNormalized(controller_, info.id);
		TChar text[128] = {};
		if (v->getParamStringByValue(controller_, info.id, out->value, text) ==
			kResultOk)
			out->text = utf8(text);
		const std::string units = utf8(info.units);
		if (!units.empty()) out->text += " " + units;
		return true;
	}

	// The controller shows it at once; the processor gets it with a block
	bool set_param(uint32_t id, double value) override {
		if (controller_ == nullptr) {
			rv_host_log("no edit controller: no parameters");
			return false;
		}
		controller_->__vftable->setParamNormalized(controller_, id, value);
		pending_.push(id, value);
		if (active_) rv_host_log("the processor gets it with the next block");
		else
			rv_host_log("the processor gets it once processing runs");
		return true;
	}

	bool save_state(std::vector<uint8_t>* out) override {
		if (component_ == nullptr) return false;
		MemStream m;
		m.base.__vftable = &g_stream_vtbl;
		const tresult r = component_->__vftable->getState(component_, &m.base);
		rv_host_log("IComponent::getState -> %d", r);
		if (r != kResultOk) return false;
		*out = std::move(m.data);
		return true;
	}

	// The component's state, then the controller's view of it, as a host
	// loading a preset does
	bool load_state(const std::vector<uint8_t>& data) override {
		if (component_ == nullptr) return false;
		MemStream m;
		m.base.__vftable = &g_stream_vtbl;
		m.data = data;
		tresult r = component_->__vftable->setState(component_, &m.base);
		rv_host_log("IComponent::setState -> %d (%lld of %zu bytes read)", r,
			(long long)m.pos, data.size());
		if (r != kResultOk) return false;

		if (controller_ != nullptr) {
			m.pos = 0;
			r = controller_->__vftable->setComponentState(controller_, &m.base);
			rv_host_log("IEditController::setComponentState -> %d", r);
		}
		return true;
	}

	void unload() override {
		destroy_controller();
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

	// The edit controller: the component itself, or a class of its own.
	// Parameters live there.
	void create_controller() {
		IComponent_vtbl* cv = component_->__vftable;
		void* obj = nullptr;
		if (cv->queryInterface(component_, IID_IEditController.b, &obj) ==
				kResultOk &&
			obj != nullptr) {
			controller_ = (IEditController*)obj;
			shared_controller_ = true;
			rv_host_log("IEditController: the component itself");

		} else {
			char cid[16] = {};
			static const char zero[16] = {};
			if (cv->getControllerClassId(component_, cid) != kResultOk ||
				!std::memcmp(cid, zero, 16)) {
				rv_host_log("no edit controller class: no parameters");
				return;
			}

			tresult r = factory_->__vftable->createInstance(
				factory_, cid, IID_IEditController.b, &obj);
			if (r != kResultOk || obj == nullptr) {
				rv_host_log("creating the edit controller failed (%d)", r);
				return;
			}

			controller_ = (IEditController*)obj;
			r = controller_->__vftable->initialize(
				controller_, (FUnknown*)&g_host_app);
			rv_host_log("IEditController @ %p, vtable @ %p: initialize -> %d",
				obj, (void*)controller_->__vftable, r);

			if (r != kResultOk) {
				controller_->__vftable->release(controller_);
				controller_ = nullptr;
				return;
			}

			connect_points(true);
		}

		IEditController_vtbl* v = controller_->__vftable;
		v->setComponentHandler(controller_, &g_handler);

		// The controller learns the component's state
		MemStream m;
		m.base.__vftable = &g_stream_vtbl;
		if (cv->getState(component_, &m.base) == kResultOk) {
			m.pos = 0;
			v->setComponentState(controller_, &m.base);
		}
		rv_host_log("%zu parameters ('params' lists them)", param_count());
	}

	// Component and controller talk through their connection points
	void connect_points(bool on) {
		void *a = nullptr, *b = nullptr;
		component_->__vftable->queryInterface(
			component_, IID_IConnectionPoint.b, &a);
		controller_->__vftable->queryInterface(
			controller_, IID_IConnectionPoint.b, &b);

		if (a != nullptr && b != nullptr) {
			auto* ca = (IConnectionPoint*)a;
			auto* cb = (IConnectionPoint*)b;

			if (on) {
				ca->__vftable->connect(ca, cb);
				cb->__vftable->connect(cb, ca);
			} else {
				ca->__vftable->disconnect(ca, cb);
				cb->__vftable->disconnect(cb, ca);
			}
			connected_ = on;
		}

		if (a) ((IConnectionPoint*)a)->__vftable->release((IConnectionPoint*)a);
		if (b) ((IConnectionPoint*)b)->__vftable->release((IConnectionPoint*)b);
	}

	void destroy_controller() {
		if (controller_ == nullptr) return;

		controller_->__vftable->setComponentHandler(controller_, nullptr);
		if (!shared_controller_) {
			if (connected_) connect_points(false);
			controller_->__vftable->terminate(controller_);
		}

		controller_->__vftable->release(controller_);
		controller_ = nullptr;
		shared_controller_ = connected_ = false;
	}

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
	HostChanges changes_ = {};
	host::ParamQueue pending_;
	std::vector<host::ParamQueue::Change> taken_;
	IEditController* controller_ = nullptr;
	bool shared_controller_ = false;
	bool connected_ = false;
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
