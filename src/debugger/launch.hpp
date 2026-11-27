#pragma once
#include "core/reverb.hpp"
#include <pro.h>
#include <vector>

/**
 * @file launch.hpp
 * @brief Points IDA debugger at reverb-minihost, with the detection results
 * of the database as its arguments
 */
namespace rv::debug {

/**
 * @brief Sets the debugger's process options to run reverb-minihost on the
 * input file as the first hostable format of @p formats
 *
 * Options pointing at another application are the user's and stay as they
 * are; ones that are unset, the input file itself (IDA's default) or an
 * earlier minihost are replaced.
 *
 * @param formats the formats detected in the database
 * @return true if the options now launch the minihost
 */
bool configure_minihost(InterfaceSet formats);

/**
 * @brief configure_minihost() again with the formats it last saw, e.g. after
 * the settings changed
 */
bool reconfigure_minihost();

/** @brief The hostable formats configure_minihost() last saw, best first */
std::vector<Interface> hostable_formats();

/**
 * @brief What the minihost is launched with; kept in the database
 *
 * The defaults are the minihost's own: only the settings that differ are
 * passed.
 */
struct host_settings_t {
	int format = -1; ///< An Interface, or -1 for the best detected one
	int class_index = 0;
	int sample_rate = 48000;
	int block_size = 512;
	qstring signal = "sine"; ///< sine, impulse, noise or silence
	bool notes = true;
	bool paused = false;
	bool step = false; ///< Wait before each lifecycle phase for Next step
	bool realtime = true;
	bool process = true;
	qstring extra; ///< Appended to the arguments as typed
};

/** @brief The settings of this database, or the defaults */
host_settings_t load_settings();

/** @brief Stores @p s in the database */
void save_settings(const host_settings_t& s);

/**
 * @brief Listens for the minihost and hooks the debugger events: while a
 * minihost configured here runs, its log is shown in the Output window
 */
void init();

/** @brief True while a minihost is connected */
bool minihost_connected();

/**
 * @brief Sends @p command to the minihost
 *
 * @param command the command to send (e.g. info, play, blocks N, quit)
 * @return false if none is connected
 */
bool send_command(const char* command);

/** @brief Undoes init() */
void term();

} // namespace rv::debug
