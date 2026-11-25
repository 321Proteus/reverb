#pragma once

#include "core/reverb.hpp"

/**
 * @file framework.hpp
 * @brief Plugin framework detection (JUCE, iPlug2, DPF) from string signals.
 */
namespace rv {

/**
 * @brief A string whose presence in the binary hints at a framework
 */
struct fw_signal_t {
	Framework fw;		 /**< Framework the signal points to */
	const char* pattern; /**< Literal searched for in the data segments */
	int weight;			 /**< Score added when found */
};

/**
 * @brief The framework's signals and optional hooks
 */
struct FrameworkModule {
	const char* types_decl;		/**< Type declarations, or null */
	const fw_signal_t* signals; /**< Detection signals */
	int num_signals;			/**< Number of @c signals */

	qstring (
		*detect_version)(); /**< Version string, or null if not detectable */
	void (*label)(Interface iface); /**< Framework-specific labeling, or null */

	/** Base name of the type libraries, e.g. `"juce"` for
	 * `rv_juce_v<version>_<abi>.til` */
	const char* til_name = nullptr;
};

/**
 * @brief Result of scan_framework()
 */
struct FrameworkInfo {
	Framework fw = Framework::OTHER; /**< OTHER if none was detected */
	qstring version;				 /**< Empty if unknown */
	int score = 0; /**< Summed weight of the matched signals */
};

/**
 * @brief Detects the framework from the signals of all modules
 *
 * The framework with the highest summed weight wins, if it reaches the
 * detection threshold.
 */
FrameworkInfo scan_framework();
/**
 * @brief Module of @p fw, or null if it has none (OTHER)
 */
const FrameworkModule* get_framework_module(Framework fw);

/**
 * @brief First hit of the literal @p lit in the data segments
 *
 * @return Its address, or BADADDR if none
 */
ea_t find_fw_string(const char* lit);

} // namespace rv