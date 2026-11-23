#pragma once

#include <pro.h>
namespace rv {
/**
 * @brief A single plugin binary format that can be detected by REverb
 *
 */
enum class Interface { VST2, VST3, AU, AAX, CLAP, _COUNT };

/**
 * @brief A list of interfaces processed in a REverb scan
 */
class InterfaceSet {
public:
	/**
	 * @brief Adds an interface to the set
	 *
	 * @param i the interface to add
	 */

	void add(Interface i) { value_ |= bit(i); }

	/**
	 * @brief Removes an interface from the set
	 *
	 * @param i the interface to remove
	 */
	void remove(Interface i) { value_ &= ~bit(i); }

	/**
	 * @brief Checks if the set contains `i`
	 *
	 * @param i the interface to check
	 * @return true if the interface is in in the set, else otherwise
	 */
	bool has(Interface i) const { return (value_ & bit(i)) != 0; }

	/**
	 * @brief Tests whether the set is empty
	 *
	 * @return true if no interfaces are in the set
	 */
	bool empty() const { return value_ == 0; }

	/**
	 * @brief Calls `fn` once for each interface in the set
	 *
	 * @tparam F a callable invoked as `fn(Interface)`
	 * @param fn the callback to execute
	 */
	template <class F> void for_each(F&& fn) const {
		for (int i = 0; i < int(Interface::_COUNT); i++)
			if (value_ & (1u << i)) fn(Interface(i));
	}

private:
	static constexpr uint32 bit(Interface i) { return 1u << unsigned(i); }
	uint32 value_ = 0;
};

/**
 * @brief A single plugin framework that can be detected by REverb
 */
enum class Framework { JUCE, IPLUG2, DPF, OTHER };

/**
 * @brief Get the interface's display name
 *
 * @param iface the interface to name
 * @return the interface name, such as "VST3". Returns "Other" for any value not
 * listed
 */
inline const char* get_interface_name(Interface iface) {
	switch (iface) {
	case Interface::VST2: return "VST2";
	case Interface::VST3: return "VST3";
	case Interface::AU:	  return "AU";
	case Interface::AAX:  return "AAX";
	case Interface::CLAP: return "CLAP";
	default:			  return "Other";
	}
}

/**
 * @brief Get the framework's display name
 *
 * @param fw the framework to name
 * @return a human-readable display name. Returns "Other or unknown framework"
 * for any value not listed
 */
inline const char* get_framework_name(Framework fw) {
	switch (fw) {
	case Framework::JUCE:	return "JUCE";
	case Framework::IPLUG2: return "iPlug2";
	case Framework::DPF:	return "Distrho Plugin Framework";
	default:				return "Other or unknown framework";
	}
}

} // namespace rv