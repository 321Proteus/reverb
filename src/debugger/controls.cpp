#include "controls.hpp"
#include "core/log.hpp"
#include "launch.hpp"
#include <kernwin.hpp>
#include <loader.hpp>
#include <pro.h>

// The icons, from assets/minihost (mid-tone, for light and dark themes)
#include "mh_blocks_svg_embed.h"
#include "mh_info_svg_embed.h"
#include "mh_pause_svg_embed.h"
#include "mh_play_svg_embed.h"
#include "mh_quit_svg_embed.h"
#include "mh_settings_svg_embed.h"

namespace rv::debug {
namespace {

const char kMenu[] = "ReverbMinihostMenu";
const char kMenuLabel[] = "REverb minihost";
const char kMenuPath[] = "Debugger/REverb minihost/";
const char kToolbar[] = "ReverbMinihostToolbar";

// An embedded icon; -1 if it can't be loaded
int load_icon(const char* src) {
	// The embedded text starts with a newline
	const char* svg = strchr(src, '<');
	if (svg == nullptr) return -1;
	const int id = load_custom_icon(svg, unsigned(strlen(svg)), "svg");
	return id > 0 ? id : -1;
}

// Sends a fixed command; enabled while a minihost is connected
struct command_ah_t : public action_handler_t {
	const char* command;
	explicit command_ah_t(const char* c) : command(c) {}

	int idaapi activate(action_activation_ctx_t*) override {
		if (!send_command(command)) rv_log("reverb-minihost is not connected");
		return 0;
	}
	action_state_t idaapi update(action_update_ctx_t*) override {
		return minihost_connected() ? AST_ENABLE : AST_DISABLE;
	}
};

//"blocks N", asking for N
struct blocks_ah_t : public command_ah_t {
	blocks_ah_t() : command_ah_t(nullptr) {}

	int idaapi activate(action_activation_ctx_t*) override {
		static sval_t last = 16;
		sval_t n = last;
		if (!ask_long(&n, "Blocks to run, then pause") || n <= 0) return 0;
		last = n;
		qstring cmd;
		cmd.sprnt("blocks %lld", (long long)n);
		if (!send_command(cmd.c_str()))
			rv_log("reverb-minihost is not connected");
		return 0;
	}
};

// Any command, typed in
struct typed_ah_t : public command_ah_t {
	typed_ah_t() : command_ah_t(nullptr) {}

	int idaapi activate(action_activation_ctx_t*) override {
		static qstring last = "info";
		qstring cmd = last;
		if (!ask_str(&cmd, HIST_CMD,
				"Minihost command (info, play, pause, blocks N, quit)"))
			return 0;
		cmd.trim2();
		if (cmd.empty()) return 0;
		last = cmd;
		if (!send_command(cmd.c_str()))
			rv_log("reverb-minihost is not connected");
		return 0;
	}
};

const char* const kSignals[] = { "sine", "impulse", "noise", "silence" };

// The settings form; true if they were changed
bool edit_settings() {
	const std::vector<Interface> hostable = hostable_formats();
	if (hostable.empty()) {
		rv_log("No format reverb-minihost can host was detected");
		return false;
	}
	host_settings_t s = load_settings();

	qstrvec_t formats;
	int format_sel = 0;
	for (size_t i = 0; i < hostable.size(); i++) {
		formats.push_back(get_interface_name(hostable[i]));
		if (int(hostable[i]) == s.format) format_sel = int(i);
	}

	qstrvec_t signals;
	int signal_sel = 0;
	for (int i = 0; i < int(qnumber(kSignals)); i++) {
		signals.push_back(kSignals[i]);
		if (s.signal == kSignals[i]) signal_sel = i;
	}

	sval_t cls = s.class_index, rate = s.sample_rate, block = s.block_size;
	ushort checks = (s.notes ? 1 : 0) | (s.paused ? 2 : 0) |
					(s.realtime ? 4 : 0) | (s.process ? 8 : 0);
	qstring extra = s.extra;

	// IDA places fields by character position: the labels are padded to one
	// length to line them up. The title is followed by two empty lines, and a
	// ':' ends a label, so hints mustn't have one (see FORM_C in kernwin.hpp).
	static const char form[] =
		"REverb minihost settings\n"
		"\n"
		"\n"
		"Used the next time the debugger starts reverb-minihost.\n"
		"\n"
		"<~F~ormat              :b:0:12::>\n"
		"<#The class or plugin of a module with several#"
		"Plugin ~i~ndex        :D:8:12::>\n"
		"\n"
		"<Sample ~r~ate (Hz)    :D:8:12::>\n"
		"<~B~lock size (frames) :D:8:12::>\n"
		"<Input ~s~ignal        :b:0:12::>\n"
		"\n"
		"<#A note every second, for instruments#Engine#How the blocks are run#"
		"Send ~n~otes:C>\n"
		"<#Activated, but no blocks run until Play#Start ~p~aused:C>\n"
		"<#Blocks paced like an audio device would#Real ~t~ime:C>\n"
		"<#When off, the plugin is only created#Pr~o~cess audio:C>>\n"
		"\n"
		"<#Passed as typed, before the plugin path#"
		"~E~xtra arguments     :q:1024:40::>\n";

	for (;;) {

		if (ask_form(form, &formats, &format_sel, &cls, &rate, &block, &signals,
				&signal_sel, &checks, &extra) != 1)
			return false;

		if (cls < 0 || rate < 1000 || rate > 768000 || block < 1 ||
			block > 65536) {
			warning("Expected a plugin index from 0, a sample rate of 1000 to "
					"768000 Hz and a block size of 1 to 65536 frames");
			continue;
		}

		break;
	}

	s.format = int(hostable[size_t(format_sel)]);
	s.class_index = int(cls);
	s.sample_rate = int(rate);
	s.block_size = int(block);
	s.signal = kSignals[signal_sel];
	s.notes = (checks & 1) != 0;
	s.paused = (checks & 2) != 0;
	s.realtime = (checks & 4) != 0;
	s.process = (checks & 8) != 0;
	s.extra = extra;
	s.extra.trim2();

	save_settings(s);
	if (reconfigure_minihost())
		rv_log("Minihost settings saved: they apply the next time it starts");
	return true;
}

struct settings_ah_t : public action_handler_t {
	int idaapi activate(action_activation_ctx_t*) override {
		edit_settings();
		return 0;
	}
	action_state_t idaapi update(action_update_ctx_t*) override {
		return hostable_formats().empty() ? AST_DISABLE : AST_ENABLE;
	}
};

command_ah_t g_play("play"), g_pause("pause"), g_info("info"), g_quit("quit");
blocks_ah_t g_blocks;
typed_ah_t g_typed;
settings_ah_t g_settings;

struct control_t {
	const char* name;
	const char* label;
	const char* tooltip;
	action_handler_t* handler;
	const char* svg; ///< Icon, or null
	bool toolbar;	 ///< Also on the toolbar
	bool needs_host; ///< Enabled only while a minihost is connected
	int icon;
};

control_t g_controls[] = {
	{ "reverb:mh_play", "Play", "Run blocks until paused", &g_play,
		RV_MH_PLAY_SVG, true, true, -1 },
	{ "reverb:mh_pause", "Pause", "Stop running blocks", &g_pause,
		RV_MH_PAUSE_SVG, true, true, -1 },
	{ "reverb:mh_blocks", "Run blocks...", "Run some blocks, then pause",
		&g_blocks, RV_MH_BLOCKS_SVG, true, true, -1 },
	{ "reverb:mh_info", "Info", "Print the plugin and engine state", &g_info,
		RV_MH_INFO_SVG, true, true, -1 },
	{ "reverb:mh_quit", "Quit", "Unload the plugin and end the minihost",
		&g_quit, RV_MH_QUIT_SVG, true, true, -1 },
	{ "reverb:mh_command", "Send command...", "Send a typed command", &g_typed,
		nullptr, false, true, -1 },
	{ "reverb:mh_settings", "Settings...", "What the minihost is started with",
		&g_settings, RV_MH_SETTINGS_SVG, true, false, -1 },
};

} // namespace

void init_controls() {
	const bool gui = is_idaq();
	if (gui) {
		create_menu(kMenu, kMenuLabel, "Debugger/");
		create_toolbar(kToolbar, kMenuLabel);
	}

	for (control_t& c : g_controls) {
		if (c.svg != nullptr && gui) c.icon = load_icon(c.svg);
		const action_desc_t desc = ACTION_DESC_LITERAL_OWNER(c.name, c.label,
			c.handler, &PLUGIN, nullptr, c.tooltip, c.icon, ADF_OT_PLUGIN);
		if (!register_action(desc)) continue;
		if (!gui) continue;
		attach_action_to_menu(kMenuPath, c.name, SETMENU_APP);
		if (c.toolbar) attach_action_to_toolbar(kToolbar, c.name);
	}
}

void refresh_controls() {
	const bool connected = minihost_connected();
	for (const control_t& c : g_controls)
		if (c.needs_host)
			update_action_state(c.name, connected ? AST_ENABLE : AST_DISABLE);
}

void term_controls() {
	for (control_t& c : g_controls) {
		if (c.toolbar) detach_action_from_toolbar(kToolbar, c.name);
		detach_action_from_menu(kMenuPath, c.name);
		unregister_action(c.name);
		if (c.icon > 0) free_custom_icon(c.icon);
		c.icon = -1;
	}
	delete_toolbar(kToolbar);
	delete_menu(kMenu);
}

} // namespace rv::debug
