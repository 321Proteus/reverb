#include "panel.hpp"
#include "core/log.hpp"
#include <kernwin.hpp>
#include <lines.hpp>
#include <loader.hpp>

namespace rv::panel {

static constexpr const char* TITLE = "REverb";
static constexpr const char* ACTION = "reverb:show_panel";
static constexpr const char* MENU = "View/Open subviews/";

static TWidget* g_widget = nullptr;
static strvec_t g_lines;

static void build() {
	g_lines.clear();
	g_lines.push_back(simpleline_t(COLSTR("Test", SCOLOR_AUTOCMT)));
}

static void idaapi on_close(TWidget* cv, void*) {
	if (cv == g_widget) g_widget = nullptr;
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

	g_widget = create_custom_viewer(
		TITLE, &first, &last, &first, nullptr, &g_lines, &HANDLERS, nullptr);
	if (g_widget == nullptr) {
		rv_log("Could not create the panel");
		return;
	}

	display_widget(g_widget, WOPN_DP_RIGHT | WOPN_RESTORE, "IDA View-A");
}

void close() {
	if (g_widget != nullptr) close_widget(g_widget, 0);
	g_widget = nullptr;
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

	const action_desc_t desc = ACTION_DESC_LITERAL_OWNER(ACTION,
		"REverb plugin info", &g_show_ah, &PLUGIN, nullptr,
		"What REverb found in this plugin", -1, ADF_OT_PLUGIN);

	if (register_action(desc)) attach_action_to_menu(MENU, ACTION, SETMENU_APP);
}

void term() {
	close();
	detach_action_from_menu(MENU, ACTION);
	unregister_action(ACTION);
}

} // namespace rv::panel
