#include "platform.hpp"
#include <chrono>
#include <thread>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <dirent.h>
#include <dlfcn.h>
#include <sys/stat.h>
#endif

#ifdef __APPLE__
#include <CoreFoundation/CoreFoundation.h>
#endif

namespace rv::host {

#ifdef _WIN32

bool SharedLib::open(const std::string& path, std::string& err) {
	handle_ = (void*)LoadLibraryA(path.c_str());
	if (handle_ == nullptr)
		err = "LoadLibrary failed, error " + std::to_string(GetLastError());
	return handle_ != nullptr;
}

void SharedLib::close() {
	if (handle_ != nullptr) FreeLibrary((HMODULE)handle_);
	handle_ = nullptr;
}

void* SharedLib::sym(const char* name) const {
	return handle_ ? (void*)GetProcAddress((HMODULE)handle_, name) : nullptr;
}

#else

bool SharedLib::open(const std::string& path, std::string& err) {
	handle_ = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
	if (handle_ == nullptr) {
		const char* e = dlerror();
		err = e ? e : "dlopen failed";
	}
	return handle_ != nullptr;
}

void SharedLib::close() {
	if (handle_ != nullptr) dlclose(handle_);
	handle_ = nullptr;
}

void* SharedLib::sym(const char* name) const {
	return handle_ ? dlsym(handle_, name) : nullptr;
}

#endif

static bool is_dir(const std::string& p) {
#ifdef _WIN32
	DWORD a = GetFileAttributesA(p.c_str());
	return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
#else
	struct stat st;
	return stat(p.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
#endif
}

static bool is_file(const std::string& p) {
#ifdef _WIN32
	DWORD a = GetFileAttributesA(p.c_str());
	return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
#else
	struct stat st;
	return stat(p.c_str(), &st) == 0 && S_ISREG(st.st_mode);
#endif
}

// Not a module
static bool is_side_file(const std::string& name) {
	static const char* const kExts[] = { ".i64", ".idb", ".id0", ".id1", ".id2",
		".nam", ".til", ".log", ".txt", ".json", ".plist", nullptr };
	const size_t dot = name.rfind('.');

	if (dot == std::string::npos) return false;

	const std::string ext = name.substr(dot);
	for (const char* const* e = kExts; *e; e++)
		if (ext == *e) return true;

	return false;
}

// The first file of @p dir that may be a module (bundles hold one binary
// per platform dir)
static std::string first_file(const std::string& dir) {
#ifdef _WIN32
	WIN32_FIND_DATAA fd;
	HANDLE h = FindFirstFileA((dir + "\\*").c_str(), &fd);
	if (h == INVALID_HANDLE_VALUE) return {};
	std::string out;
	do {
		if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) &&
			!is_side_file(fd.cFileName)) {
			out = dir + "\\" + fd.cFileName;
			break;
		}
	} while (FindNextFileA(h, &fd));
	FindClose(h);
	return out;
#else
	DIR* d = opendir(dir.c_str());
	if (d == nullptr) return {};

	std::string out;
	while (dirent* e = readdir(d)) {
		if (e->d_name[0] == '.') continue;

		std::string p = dir + "/" + e->d_name;
		if (!is_dir(p) && !is_side_file(e->d_name)) {
			out = p;
			break;
		}
	}

	closedir(d);
	return out;

#endif
}

std::string resolve_binary(const std::string& path) {
	if (!is_dir(path)) return path;

	std::string base = path;
	while (base.size() > 1 && (base.back() == '/' || base.back() == '\\'))
		base.pop_back();

#if defined(_WIN32)
#if defined(_M_ARM64)
	const char* sub = "arm64-win";
#else
	const char* sub = "x86_64-win";
#endif
	std::string dir = base + "\\Contents\\" + sub;
#elif defined(__APPLE__)
	std::string dir = base + "/Contents/MacOS";
#else
#if defined(__aarch64__)
	const char* sub = "aarch64-linux";
#else
	const char* sub = "x86_64-linux";
#endif
	std::string dir = base + "/Contents/" + sub;
#endif
	// Named after the bundle by convention (X.vst3 holds X, X.vst3 or X.so),
	// else the first file that may be a module
	const size_t sep = base.find_last_of("/\\");
	std::string stem = sep == std::string::npos ? base : base.substr(sep + 1);
	const size_t dot = stem.rfind('.');
	if (dot != std::string::npos && dot > 0) stem.resize(dot);
#if defined(_WIN32)
	const char* const kNames[] = { ".vst3", ".clap", "", nullptr };
	const char* sep_s = "\\";
#elif defined(__APPLE__)
	const char* const kNames[] = { "", nullptr };
	const char* sep_s = "/";
#else
	const char* const kNames[] = { ".so", "", nullptr };
	const char* sep_s = "/";
#endif
	for (const char* const* n = kNames; *n; n++) {
		const std::string bin = dir + sep_s + stem + *n;
		if (is_file(bin)) return bin;
	}

	std::string bin = first_file(dir);
	return bin.empty() ? path : bin;
}

static std::string parent(const std::string& p) {
	size_t s = p.find_last_of("/\\");
	return s == std::string::npos ? std::string() : p.substr(0, s);
}

static std::string base_name(const std::string& p) {
	size_t s = p.find_last_of("/\\");
	return s == std::string::npos ? p : p.substr(s + 1);
}

std::string bundle_root(const std::string& path) {
	if (is_dir(path)) return path;
	// <bundle>/Contents/<MacOS | x86_64-linux | x86_64-win | ...>/<binary>
	std::string contents = parent(parent(path));
	if (base_name(contents) != "Contents") return path;
	std::string bundle = parent(contents);
	return is_dir(bundle) ? bundle : path;
}

void pump_main_thread(int ms) {
#ifdef __APPLE__
	CFRunLoopRunInMode(kCFRunLoopDefaultMode, ms / 1000.0, false);
#else
	std::this_thread::sleep_for(std::chrono::milliseconds(ms));
#endif
}

void* create_bundle_ref(const std::string& bundle_path) {
#ifdef __APPLE__
	CFURLRef url = CFURLCreateFromFileSystemRepresentation(nullptr,
		(const UInt8*)bundle_path.c_str(), (CFIndex)bundle_path.size(), true);
	if (url == nullptr) return nullptr;
	CFBundleRef b = CFBundleCreate(nullptr, url);
	CFRelease(url);
	return (void*)b;
#else
	(void)bundle_path;
	return nullptr;
#endif
}

void release_bundle_ref(void* ref) {
#ifdef __APPLE__
	if (ref != nullptr) CFRelease((CFTypeRef)ref);
#else
	(void)ref;
#endif
}

} // namespace rv::host
