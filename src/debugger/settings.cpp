/**
 * @file settings.cpp
 * @brief The minihost settings, in a netnode of the database
 */
#include "settings.hpp"
#include <netnode.hpp>
#include <random>

namespace rv::debug {

static const char kSettingsNode[] = "$ reverb minihost";
enum : nodeidx_t { // altvals; supstrs share the indices
	SET_SAVED,	   // 1 once saved
	SET_FORMAT,	   // the Interface + 1, 0 for the best detected
	SET_CLASS,
	SET_RATE,
	SET_BLOCK,
	SET_FLAGS,			 // FLAG_*
	SET_SIGNAL,			 // supstr
	SET_EXTRA,			 // supstr
	SET_REMOTE_MINIHOST, // supstr
	SET_REMOTE_PLUGIN,	 // supstr
	SET_REMOTE_PORT,
	SET_TOKEN, // supstr, made once per database
};

enum : nodeidx_t {
	FLAG_NOTES = 1,
	FLAG_PAUSED = 2,
	FLAG_FAST = 4,
	FLAG_NO_PROCESS = 8,
	FLAG_STEP = 16
};

host_settings_t load_settings() {

	host_settings_t s;
	netnode n(kSettingsNode);
	if (n == BADNODE || n.altval(SET_SAVED) != 1) return s;

	s.format = int(n.altval(SET_FORMAT)) - 1;
	s.class_index = int(n.altval(SET_CLASS));
	s.sample_rate = int(n.altval(SET_RATE));
	s.block_size = int(n.altval(SET_BLOCK));
	const nodeidx_t f = n.altval(SET_FLAGS);

	s.notes = (f & FLAG_NOTES) != 0;
	s.paused = (f & FLAG_PAUSED) != 0;
	s.realtime = (f & FLAG_FAST) == 0;
	s.process = (f & FLAG_NO_PROCESS) == 0;
	s.step = (f & FLAG_STEP) != 0;
	n.supstr(&s.signal, SET_SIGNAL);
	n.supstr(&s.extra, SET_EXTRA);
	n.supstr(&s.remote_minihost, SET_REMOTE_MINIHOST);
	n.supstr(&s.remote_plugin, SET_REMOTE_PLUGIN);
	if (const nodeidx_t port = n.altval(SET_REMOTE_PORT)) // 0 before it existed
		s.remote_port = int(port);
	return s;
}

void save_settings(const host_settings_t& s) {
	netnode n(kSettingsNode, 0, true);
	n.altset(SET_FORMAT, nodeidx_t(s.format + 1));
	n.altset(SET_CLASS, nodeidx_t(s.class_index));
	n.altset(SET_RATE, nodeidx_t(s.sample_rate));
	n.altset(SET_BLOCK, nodeidx_t(s.block_size));
	n.altset(SET_FLAGS,
		(s.notes ? FLAG_NOTES : 0) | (s.paused ? FLAG_PAUSED : 0) |
			(s.realtime ? 0 : FLAG_FAST) | (s.process ? 0 : FLAG_NO_PROCESS) |
			(s.step ? FLAG_STEP : 0));
	n.supset(SET_SIGNAL, s.signal.c_str());
	n.supset(SET_EXTRA, s.extra.c_str());
	n.supset(SET_REMOTE_MINIHOST, s.remote_minihost.c_str());
	n.supset(SET_REMOTE_PLUGIN, s.remote_plugin.c_str());
	n.altset(SET_REMOTE_PORT, nodeidx_t(s.remote_port));
	n.altset(SET_SAVED, 1);
}

qstring channel_token() {
	netnode n(kSettingsNode, 0, true);
	qstring token;
	if (n.supstr(&token, SET_TOKEN) > 0 && !token.empty()) return token;

	std::random_device rd;
	for (int i = 0; i < 4; i++)
		token.cat_sprnt("%08x", unsigned(rd()));
	n.supset(SET_TOKEN, token.c_str());
	return token;
}

} // namespace rv::debug
