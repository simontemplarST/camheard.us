// Quick open -- Ctrl+P, one box that reaches every page and every command.
//
// The app has six tabs and a couple of dozen buttons, which is about the
// point where hunting for the right one costs more than typing three letters
// at it. Pages and commands share the list on purpose: "abt" and "new" are
// the same gesture, and neither needs you to know which tab it lives on.
//
// The shortcut sheet (F1) lives here too, because the two answer the same
// question from opposite ends.
#include "app.h"
#include "fuzzy.h"
#include "util.h"
#include "widgets.h"

#include "imgui.h"
#include "imgui_stdlib.h"

#include <algorithm>
#include <cfloat>
#include <functional>

namespace {

struct Entry {
	std::string label;
	std::string hint;              // the path, or what the command does
	std::function<void(App &)> run;
	bool is_page = false;
	// Filled in per keystroke.
	int score = 0;
	std::vector<int> pos;          // matched offsets in `label`, for the highlight
};

std::vector<Entry> BuildEntries(App &a) {
	std::vector<Entry> v;

	// Pages first, so an empty box is a list of what there is to write.
	for (const site::Post &p : a.site.posts) {
		Entry e;
		e.label = p.title + (p.draft ? "  (draft)" : "");
		e.hint = "content/" + p.rel;
		e.is_page = true;
		std::string path = p.path;
		e.run = [path](App &app) { app.RequestOpen(path); };
		v.push_back(e);
	}

	auto cmd = [&](const char *label, const char *hint, std::function<void(App &)> fn) {
		Entry e;
		e.label = label;
		e.hint = hint;
		e.run = std::move(fn);
		v.push_back(e);
	};

	cmd("New post", "Ctrl+N", [](App &app) { app.want_new_post = true; });
	cmd("Save", "Ctrl+S", [](App &app) {
		if (app.ed.dirty) app.SavePost();
		if (app.cfg_dirty) app.SaveConfig();
		if (!app.ed.dirty && !app.cfg_dirty) app.Notify("nothing to save");
	});
	cmd("Rescan the site", "Ctrl+R -- re-read content/ from disk", [](App &app) {
		app.ReloadAll();
		app.Notify("rescanned the site");
	});
	cmd("Find in this post", "Ctrl+F", [](App &app) {
		if (app.ed.path.empty()) {
			app.Notify("nothing open to search", true);
			return;
		}
		app.tab = Tab::Editor;
		app.ed.find_open = true;
		app.ed.focus_find = true;
	});
	cmd("Toggle the outline", "the headings rail beside the source", [](App &app) {
		app.ed.show_outline = !app.ed.show_outline;
		app.tab = Tab::Editor;
	});
	cmd("Run the site check", "broken links, missing images, orphaned files", [](App &app) {
		app.audit_stale = true;
		app.tab = Tab::Check;
	});
	cmd("Start or stop the preview server", "hugo server -D -E -F", [](App &app) { app.StartServer(); });
	cmd("Open the local preview", "in a browser", [](App &app) {
		util::OpenURL("http://localhost:" + std::to_string(app.server_port) + "/");
	});
	cmd("Build the site", "hugo --gc --minify, as a pre-flight", [](App &app) {
		app.tab = Tab::Publish;
		app.RunTask({"hugo", "--gc", "--minify", "--cleanDestinationDir"});
	});
	cmd("Commit and push", "the Publish tab", [](App &app) { app.tab = Tab::Publish; });
	cmd("Open the live site", a.BaseURL().c_str(), [](App &app) {
		std::string base = app.BaseURL();
		if (base.empty()) app.Notify("no baseURL set in hugo.toml", true);
		else util::OpenURL(base);
	});
	cmd("Keyboard shortcuts", "F1", [](App &app) { app.want_shortcuts = true; });

	static const struct { const char *name; Tab tab; } tabs[] = {
	    {"Content", Tab::Content}, {"Editor", Tab::Editor}, {"Design", Tab::Design},
	    {"Media", Tab::Media},     {"Check", Tab::Check},   {"Publish", Tab::Publish},
	};
	for (const auto &t : tabs) {
		Tab want = t.tab;
		cmd((std::string("Go to ") + t.name).c_str(), "tab", [want](App &app) { app.tab = want; });
	}
	return v;
}

// The label with the matched letters picked out. Drawn straight onto the
// draw list rather than as a row of Text() items: the row is a single
// Selectable, and painting over it with layout items means fighting the
// cursor (and tripping ImGui's "SetCursorPos extended the parent" check).
float DrawHighlighted(ImDrawList *dl, ImVec2 p, ImFont *font, float px, const std::string &text,
                      const std::vector<int> &pos, bool selected) {
	ImFont *f = font ? font : ImGui::GetFont();
	ImU32 plain = selected ? Theme::TEXT : Theme::TEXT_DIM;
	size_t pi = 0;
	float x = p.x;
	for (size_t i = 0; i < text.size();) {
		bool hit = (pi < pos.size() && (size_t)pos[pi] == i);
		size_t run;
		if (hit) {
			run = i + 1; // matched letters are drawn one at a time
			pi++;
		} else {
			run = i;
			while (run < text.size() && !(pi < pos.size() && (size_t)pos[pi] == run)) run++;
		}
		const char *b = text.c_str() + i, *e = text.c_str() + run;
		dl->AddText(f, px, ImVec2(x, p.y), hit ? Theme::ACCENT : plain, b, e);
		x += f->CalcTextSizeA(px, FLT_MAX, 0.0f, b, e).x;
		i = run;
	}
	return x;
}

} // namespace

void DrawPalette(App &a) {
	if (a.want_palette) {
		ImGui::OpenPopup("##quickopen");
		a.palette_q.clear();
		a.palette_sel = 0;
		a.want_palette = false;
	}

	const ImGuiViewport *vp = ImGui::GetMainViewport();
	ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + vp->WorkSize.x * 0.5f, vp->WorkPos.y + 110),
	                        ImGuiCond_Always, ImVec2(0.5f, 0));
	ImGui::SetNextWindowSize(ImVec2(720, 0), ImGuiCond_Always);
	if (!ImGui::BeginPopupModal("##quickopen", nullptr,
	                            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
	                                ImGuiWindowFlags_NoMove | ImGuiWindowFlags_AlwaysAutoResize))
		return;

	if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
	ImGui::SetNextItemWidth(-1);
	ImGui::PushFont(Theme::F_UI, Theme::UI_PX * 1.1f);
	bool go = ImGui::InputTextWithHint("##q", "go to a page, or run a command", &a.palette_q,
	                                   ImGuiInputTextFlags_EnterReturnsTrue);
	ImGui::PopFont();

	std::vector<Entry> all = BuildEntries(a);
	std::vector<Entry> hits;
	for (Entry &e : all) {
		fuzzy::Match ml = fuzzy::Score(e.label, a.palette_q);
		fuzzy::Match mh = fuzzy::Score(e.hint, a.palette_q);
		if (!ml.hit && !mh.hit) continue;
		// A match in the label is what was meant; a match in the path is a
		// fallback, and scored as one.
		if (ml.hit && (!mh.hit || ml.score >= mh.score - 8)) {
			e.score = ml.score;
			e.pos = ml.pos;
		} else {
			e.score = mh.score - 8;
		}
		hits.push_back(e);
	}
	// stable_sort, so an empty query leaves the list in build order: pages,
	// then commands.
	std::stable_sort(hits.begin(), hits.end(), [](const Entry &x, const Entry &y) { return x.score > y.score; });
	if (hits.size() > 40) hits.resize(40);

	if (a.palette_sel >= (int)hits.size()) a.palette_sel = (int)hits.size() - 1;
	if (a.palette_sel < 0) a.palette_sel = 0;
	// Arrow keys work while the box has focus: a single-line InputText does
	// not use up or down itself.
	if (ImGui::IsKeyPressed(ImGuiKey_DownArrow, true)) a.palette_sel++;
	if (ImGui::IsKeyPressed(ImGuiKey_UpArrow, true)) a.palette_sel--;
	if (!hits.empty()) {
		a.palette_sel = (a.palette_sel + (int)hits.size()) % (int)hits.size();
	}

	ImGui::Dummy(ImVec2(0, 4));
	ImGui::BeginChild("hits", ImVec2(0, 380), ImGuiChildFlags_None);
	// Scrolling the selection into view only when it actually moved, so the
	// mouse wheel still works while the keyboard selection sits still.
	static int last_sel = -1;
	bool moved = (a.palette_sel != last_sel);
	last_sel = a.palette_sel;
	int run_index = -1;
	for (int i = 0; i < (int)hits.size(); i++) {
		const Entry &e = hits[i];
		bool sel = (i == a.palette_sel);
		ImGui::PushID(i);
		if (ImGui::Selectable("##row", sel, ImGuiSelectableFlags_AllowOverlap, ImVec2(0, 26))) run_index = i;
		if (sel && moved) ImGui::SetScrollHereY(0.5f);
		ImVec2 rmin = ImGui::GetItemRectMin(), rmax = ImGui::GetItemRectMax();
		ImDrawList *dl = ImGui::GetWindowDrawList();
		DrawHighlighted(dl, ImVec2(rmin.x + 8, rmin.y + 4), e.is_page ? Theme::F_UI : Theme::F_BOLD,
		                Theme::UI_PX * 0.95f, e.label, e.pos, sel);
		if (!e.hint.empty()) {
			ImFont *mono = Theme::F_MONO ? Theme::F_MONO : ImGui::GetFont();
			float px = Theme::UI_PX * 0.78f;
			float w = mono->CalcTextSizeA(px, FLT_MAX, 0.0f, e.hint.c_str()).x;
			dl->AddText(mono, px, ImVec2(rmax.x - w - 8, rmin.y + 7), Theme::TEXT_FAINT, e.hint.c_str());
		}
		ImGui::PopID();
	}
	if (hits.empty()) {
		ImGui::PushStyleColor(ImGuiCol_Text, Theme::V4(Theme::TEXT_FAINT));
		ImGui::TextWrapped("Nothing matches \"%s\".", a.palette_q.c_str());
		ImGui::PopStyleColor();
	}
	ImGui::EndChild();

	ImGui::PushFont(Theme::F_UI, Theme::UI_PX * 0.78f);
	ImGui::PushStyleColor(ImGuiCol_Text, Theme::V4(Theme::TEXT_FAINT));
	ImGui::TextUnformatted("up/down to move   ·   enter to go   ·   esc to close");
	ImGui::PopStyleColor();
	ImGui::PopFont();

	if (go && !hits.empty()) run_index = a.palette_sel;
	if (run_index >= 0 && run_index < (int)hits.size()) {
		std::function<void(App &)> fn = hits[run_index].run;
		ImGui::CloseCurrentPopup();
		fn(a);
	}
	if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) ImGui::CloseCurrentPopup();
	ImGui::EndPopup();
}

// ------------------------------------------------------------- shortcuts

void DrawShortcutsWindow(App &a) {
	if (a.want_shortcuts) {
		ImGui::OpenPopup("Keyboard shortcuts");
		a.want_shortcuts = false;
	}
	const ImGuiViewport *vp = ImGui::GetMainViewport();
	ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + vp->WorkSize.x * 0.5f, vp->WorkPos.y + vp->WorkSize.y * 0.5f),
	                        ImGuiCond_Always, ImVec2(0.5f, 0.5f));
	if (!ImGui::BeginPopupModal("Keyboard shortcuts", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return;

	struct S { const char *keys; const char *what; };
	static const S global[] = {
	    {"Ctrl+P", "quick open: any page, any command"},
	    {"Ctrl+N", "new post"},
	    {"Ctrl+S", "save the open post and hugo.toml"},
	    {"Ctrl+R", "rescan content/ from disk"},
	    {"F1", "this sheet"},
	};
	static const S editor[] = {
	    {"Ctrl+B / Ctrl+I", "bold / italic -- both toggle"},
	    {"Ctrl+K", "insert a link around the selection"},
	    {"Ctrl+F", "find and replace in this post"},
	    {"Enter / Shift+Enter", "in the find bar: next / previous match"},
	    {"Esc", "close the find bar"},
	};
	auto table = [](const char *title, const S *rows, int n) {
		W::SectionHeader(title);
		if (ImGui::BeginTable(title, 2, ImGuiTableFlags_SizingFixedFit)) {
			for (int i = 0; i < n; i++) {
				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0);
				ImGui::PushFont(Theme::F_MONO, Theme::UI_PX * 0.86f);
				ImGui::TextUnformatted(rows[i].keys);
				ImGui::PopFont();
				ImGui::TableSetColumnIndex(1);
				ImGui::PushStyleColor(ImGuiCol_Text, Theme::V4(Theme::TEXT_DIM));
				ImGui::TextUnformatted(rows[i].what);
				ImGui::PopStyleColor();
			}
			ImGui::EndTable();
		}
	};
	table("Anywhere", global, IM_ARRAYSIZE(global));
	table("In the editor", editor, IM_ARRAYSIZE(editor));

	ImGui::Dummy(ImVec2(0, 8));
	if (ImGui::Button("Close", ImVec2(110, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape, false))
		ImGui::CloseCurrentPopup();
	ImGui::EndPopup();
}
