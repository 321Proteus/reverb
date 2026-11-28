#pragma once
#include <pro.h>

/**
 * @file settings.hpp
 * @brief What reverb-minihost is launched with, kept in the database
 */
namespace rv::debug {

/**
 * @brief What the minihost is launched with
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
	qstring extra;		///< Appended to the arguments as typed
	qstring state_file; ///< Loaded after init (--state); empty for none

	// With a remote debugger: the target's paths, and the minihost's port
	qstring remote_minihost; ///< Empty: next to the plugin's bundle
	qstring remote_plugin;	 ///< Empty: the input file's path
	int remote_port = 23950;
};

/** @brief The settings of this database, or the defaults */
host_settings_t load_settings();

/** @brief Stores @p s in the database */
void save_settings(const host_settings_t& s);

/**
 * @brief What the plugin says first when it connects to a remote minihost,
 * so that no one else on the target's network drives it. Made once and kept
 * in the database, as the process options that carry it are.
 */
qstring channel_token();

} // namespace rv::debug
