#include "panel.hpp"
#include "core/log.hpp"
#include <kernwin.hpp>
#include <lines.hpp>
#include <cstring>
#include <loader.hpp>
#include "logo_svg_embed.h"

namespace rv::panel {

static constexpr const char* TITLE = "REverb";
static constexpr const char* ACTION = "reverb:show_panel";
static constexpr const char* MENU = "View/Open subviews/";

static TWidget* g_widget = nullptr;
static TWidget* g_viewer = nullptr;

static int g_icon = -1;
static strvec_t g_lines;

static void build() {
	g_lines.clear();
	g_lines.push_back(simpleline_t(COLSTR("Test", SCOLOR_AUTOCMT)));
}

static void idaapi on_close(TWidget* cv, void*) {
	if (cv != g_viewer) return;
	g_viewer = nullptr;
	g_widget = nullptr;
}

static int load_logo() {
	const char* svg = strchr(RV_LOGO_SVG, '<');
	if (svg == nullptr) return -1;

	const int id = load_custom_icon(svg, unsigned(strlen(svg)), "svg");
	if (id <= 0) rv_log("Could not load the panel icon");
	return id > 0 ? id : -1;
}

static const custom_viewer_handlers_t HANDLERS(
	nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, on_close);

void show() {
	if (!is_idaq()) return;

	if (g_widget != nullptr) {
		activate_widget(g_widget, true);
		return;
	}

	build();
	simpleline_place_t first, last(int(g_lines.size()) - 1);

	// create_custom_viewer takes no icon, so it goes inside a container
	g_widget = create_empty_widget(TITLE, g_icon);
	g_viewer = g_widget == nullptr
				   ? nullptr
				   : create_custom_viewer(TITLE, &first, &last, &first, nullptr,
						 &g_lines, &HANDLERS, nullptr, g_widget);

	if (g_viewer == nullptr) {
		rv_log("Could not create the panel");
		if (g_widget != nullptr) close_widget(g_widget, 0);
		g_widget = nullptr;
		return;
	}

	display_widget(g_widget, WOPN_DP_RIGHT | WOPN_RESTORE, "IDA View-A");
}

void close() {
	if (g_widget != nullptr) close_widget(g_widget, 0);
	g_widget = nullptr;
	g_viewer = nullptr;
	g_lines.clear();
}

namespace {
struct show_ah_t : public action_handler_t {

	int idaapi activate(action_activation_ctx_t*) override {
		show();
		return 1;
	}

	action_state_t idaapi update(action_update_ctx_t*) override {
		return AST_ENABLE_ALWAYS;
	}
};

show_ah_t g_show_ah;

} // namespace

void init() {
	g_icon = load_logo();

	const action_desc_t desc = ACTION_DESC_LITERAL_OWNER(ACTION,
		"REverb plugin info", &g_show_ah, &PLUGIN, nullptr,
		"What REverb found in this plugin", g_icon, ADF_OT_PLUGIN);

	if (register_action(desc)) attach_action_to_menu(MENU, ACTION, SETMENU_APP);
}

void term() {
	close();
	detach_action_from_menu(MENU, ACTION);
	unregister_action(ACTION);

	if (g_icon > 0) free_custom_icon(g_icon);
	g_icon = -1;
}

} // namespace rv::panel
