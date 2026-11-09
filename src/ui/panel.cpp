#include "panel.hpp"
#include "core/log.hpp"
#include "report.hpp"
#include <algorithm>
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
static std::vector<ea_t> g_eas;

// Bytes below 0x20 are color tags, and the recovered strings usually have some
static qstring printable(const qstring& s) {
	qstring r = s;
	for (size_t i = 0; i < r.length(); i++)
		if (uchar(r[i]) < 0x20) r[i] = '?';
	return r;
}

static void add(const qstring& line, ea_t ea = BADADDR) {
	g_lines.push_back(simpleline_t(line));
	g_eas.push_back(ea);
}

static void build() {
	g_lines.clear();
	g_eas.clear();

	const auto& es = report::entries();
	if (es.empty()) {
		add(COLSTR("The scan will run once the IDA autoanalysis is finished",
			SCOLOR_AUTOCMT));
		return;
	}

	for (size_t i = 0; i < es.size(); i++) {
		const report::report_entry_t& e = es[i];

		if (e.heading) {
			if (i != 0) add("");

			qstring h;
			h.sprnt(COLSTR("%s", SCOLOR_DNAME), printable(e.key).c_str());
			add(h, e.ea);
			continue;
		}

		size_t w = 0;
		for (size_t j = i; j < es.size() && !es[j].heading; j++)
			w = std::max(w, es[j].key.length());

		for (; i < es.size() && !es[i].heading; i++) {
			const qstring v = printable(es[i].value);
			const char* color = v.starts_with("\"")	 ? SCOLOR_DSTR
								: v.starts_with("(") ? SCOLOR_AUTOCMT
													 : SCOLOR_DEFAULT;
			qstring l;
			l.sprnt("  %-*s  %s%s%s%s%s", int(w), printable(es[i].key).c_str(),
				SCOLOR_ON, color, v.c_str(), SCOLOR_OFF, color);
			add(l, es[i].ea);
		}
		i--;
	}
}

static ea_t ea_at(TWidget* cv) {
	int x, y;
	place_t* p = get_custom_viewer_place(cv, false, &x, &y);
	if (p == nullptr) return BADADDR;

	const uint32 n = static_cast<simpleline_place_t*>(p)->n;
	return n < g_eas.size() ? g_eas[n] : BADADDR;
}

static bool idaapi on_dblclick(TWidget* cv, int, void*) {
	const ea_t ea = ea_at(cv);
	if (ea == BADADDR) return false;

	jumpto(ea);
	return true;
}

static bool idaapi on_key(TWidget* cv, int key, int shift, void*) {
	return key == IK_RETURN && shift == 0 && on_dblclick(cv, 0, nullptr);
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
	on_key, nullptr, nullptr, nullptr, on_dblclick, nullptr, on_close);

void show() {
	if (!is_idaq()) return;

	build();
	simpleline_place_t first, last(int(g_lines.size()) - 1);

	if (g_widget != nullptr) {
		set_custom_viewer_range(g_viewer, &first, &last);
		jumpto(g_viewer, &first, 0, 0);
		refresh_custom_viewer(g_viewer);
		activate_widget(g_widget, true);
		return;
	}

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
	g_eas.clear();
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

struct show_req_t : public ui_request_t {
	bool idaapi run() override {
		show();
		return false;
	}
};

} // namespace

void show_later() {
	if (is_idaq()) execute_ui_requests(new show_req_t, nullptr);
}

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
