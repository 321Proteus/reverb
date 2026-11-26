/**
 * @file host.cpp
 * @brief CLAP for reverb-minihost: the part of the ABI it calls, and the
 * ids from exts.inc
 */
#include "formats/host.hpp"
#include <atomic>
#include <cstdint>
#include <cstring>

namespace rv::clap {
namespace {

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
	int32_t (*process)(const clap_plugin* plugin, const void* process);
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
#define RV_CLAP_EXT(Name, id, plugin_s, host_s)
#include "exts.inc"
#undef RV_CLAP_FACTORY
#undef RV_CLAP_EXT

struct ExtName {
	const char* id;
	const char* name;
};
const ExtName kExtNames[] = {
#define RV_CLAP_FACTORY(Name, id, s)
#define RV_CLAP_EXT(Name, id, plugin_s, host_s) { id, #Name },
#include "exts.inc"
#undef RV_CLAP_FACTORY
#undef RV_CLAP_EXT
};

const char* ext_name(const char* id) {
	for (const ExtName& e : kExtNames)
		if (!std::strcmp(e.id, id)) return e.name;
	return "unknown";
}

std::atomic<bool> g_callback_requested{ false };

const void* host_get_extension(const clap_host*, const char* id) {
	rv_host_log("host extension requested: %s (%s)", id, ext_name(id));
	return nullptr;
}

void host_request_restart(const clap_host*) { rv_host_log("request_restart"); }
void host_request_process(const clap_host*) { rv_host_log("request_process"); }
void host_request_callback(const clap_host*) { g_callback_requested = true; }

const clap_host_t g_host = { { 1, 2, 0 }, nullptr, "reverb-minihost", "REverb",
	"", "0.1", host_get_extension, host_request_restart, host_request_process,
	host_request_callback };

class ClapHost final : public host::PluginHost {
public:
	Interface format() const override { return Interface::CLAP; }

	bool load(host::SharedLib& lib, const host::LoadOptions& opts) override {
		// clap_entry is a data symbol: the struct itself, not a function
		entry_ = (const clap_plugin_entry*)lib.sym("clap_entry");
		if (entry_ == nullptr) {
			rv_host_log("clap_entry is not exported");
			return false;
		}

		rv_host_log("entry point: clap_entry @ %p (CLAP %u.%u.%u)",
			(const void*)entry_, entry_->clap_version.major,
			entry_->clap_version.minor, entry_->clap_version.revision);

		if (!entry_->init(opts.bundle_path.c_str())) {
			rv_host_log("clap_entry.init failed");
			entry_ = nullptr;
			return false;
		}

		inited_ = true;

		auto* factory =
			(const clap_plugin_factory*)entry_->get_factory(kPLUGINFactory);
		if (factory == nullptr) {
			rv_host_log("no %s", kPLUGINFactory);
			return false;
		}

		unsigned n = factory->get_plugin_count(factory);
		for (unsigned i = 0; i < n; ++i) {
			const clap_plugin_descriptor* d =
				factory->get_plugin_descriptor(factory, i);
			if (d) rv_host_log("  plugin %u: %s (%s)", i, d->name, d->id);
		}

		if (opts.class_index < 0 || (unsigned)opts.class_index >= n) {
			rv_host_log("no plugin #%d", opts.class_index);
			return false;
		}

		const clap_plugin_descriptor* d =
			factory->get_plugin_descriptor(factory, (unsigned)opts.class_index);
		plugin_ = d ? factory->create_plugin(factory, &g_host, d->id) : nullptr;
		if (plugin_ == nullptr) {
			rv_host_log("create_plugin failed");
			return false;
		}

		rv_host_log("clap_plugin @ %p: init=%p process=%p",
			(const void*)plugin_, (void*)plugin_->init,
			(void*)plugin_->process);

		bool ok = plugin_->init(plugin_);
		rv_host_log("clap_plugin.init -> %d", ok);

		if (!ok) {
			plugin_->destroy(plugin_);
			plugin_ = nullptr;
		}

		return ok;
	}

	void idle() override {
		if (plugin_ != nullptr && g_callback_requested.exchange(false) &&
			plugin_->on_main_thread)
			plugin_->on_main_thread(plugin_);
	}

	void describe() const override {
		if (plugin_ != nullptr)
			rv_host_log("CLAP \"%s\" by \"%s\" v%s", plugin_->desc->name,
				plugin_->desc->vendor, plugin_->desc->version);
	}

	void unload() override {
		if (plugin_ != nullptr) {
			plugin_->destroy(plugin_);
			plugin_ = nullptr;
		}
		if (inited_ && entry_ != nullptr) entry_->deinit();
		inited_ = false;
	}

private:
	const clap_plugin_entry* entry_ = nullptr;
	const clap_plugin* plugin_ = nullptr;
	bool inited_ = false;
};

std::unique_ptr<host::PluginHost> create() {
	return std::make_unique<ClapHost>();
}

} // namespace

const HostModule host_module = { Interface::CLAP, &create };

} // namespace rv::clap
