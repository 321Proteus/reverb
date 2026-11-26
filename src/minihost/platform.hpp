#pragma once
#include <string>

namespace rv::host {

// Thin wrapper over dlopen / LoadLibrary
class SharedLib {
public:
	SharedLib() = default;
	SharedLib(const SharedLib&) = delete;
	SharedLib& operator=(const SharedLib&) = delete;
	~SharedLib() { close(); }

	bool open(const std::string& path, std::string& err);
	void close();
	void* sym(const char* name) const;
	void* handle() const { return handle_; }

private:
	void* handle_ = nullptr;
};

// Resolves a bundle directory to the binary inside it.
// Plain files are returned as they are.
std::string resolve_binary(const std::string& path);

// The bundle directory a binary sits in (X.vst3/Contents/<platform>/X ->
// X.vst3), or the path itself: IDA only knows the inner binary
std::string bundle_root(const std::string& path);

/**
 * @brief Pumps the main thread's event loop for ~ms milliseconds
 *
 * On MacOS, CFRunLoop is used instead of a normal sleep. Frameworks often
 * deliver main-thread work this way
 */
void pump_main_thread(int ms);

/** @brief CFBundleRef of a bundle directory (macOS), nullptr elsewhere */
void* create_bundle_ref(const std::string& bundle_path);
void release_bundle_ref(void* ref);

} // namespace rv::host
