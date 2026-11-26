#include "host.hpp"
#include "formats/host.hpp"
#include <algorithm>
#include <cctype>

namespace rv::host {

/**
 * @brief Create a lowercase version of the input string
 */
static std::string lower(std::string s) {
	std::transform(s.begin(), s.end(), s.begin(),
		[](unsigned char c) { return (char)std::tolower(c); });
	return s;
}

std::optional<Interface> parse_interface(const std::string& name) {
	std::string n = lower(name);

	for (int i = 0; i < int(Interface::_COUNT); i++) {
		Interface iface = Interface(i);

		const HostModule* m = get_host_module(iface);
		if (m != nullptr && lower(get_interface_name(iface)) == n) return iface;
	}

	return std::nullopt;
}

} // namespace rv::host
