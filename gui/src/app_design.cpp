// Design tab -- the site's own UI, edited against a live mock of it.
//
// Everything on the right writes into hugo.toml through tomledit, which
// rewrites one line at a time and leaves the file's comments and layout
// alone. Everything on the left is a drawing of what those settings produce:
// the header, the home page, a post list. Clicking a piece of the mock jumps
// the inspector to the setting behind it, which is the whole point -- the
// alternative is guessing which of forty keys draws the thing you are
// looking at.
#include "app.h"
#include "util.h"
#include "widgets.h"

#include "imgui.h"
#include "imgui_stdlib.h"

#include <algorithm>
#include <cmath>

namespace {

enum class Sel { None, Title, Menu, Profile, Buttons, Social, PostMeta, Footer };
Sel g_sel = Sel::None;
int g_sel_idx = -1;

struct MenuEntry {
	std::string identifier, name, url;
	double weight = 0;
};

std::vector<MenuEntry> ReadMenu(const App &a) {
	std::vector<MenuEntry> out;
	for (const auto &t : a.cfg.GetTables("menu.main")) {
		MenuEntry m;
		m.identifier = t.GetString("identifier");
		m.name = t.GetString("name");
		m.url = t.GetString("url");
		m.weight = atof(t.Get("weight").c_str());
		out.push_back(m);
	}
	std::sort(out.begin(), out.end(), [](const MenuEntry &x, const MenuEntry &y) { return x.weight < y.weight; });
	return out;
}

void WriteMenu(App &a, const std::vector<MenuEntry> &menu) {
	std::vector<tomledit::Doc::Table> tables;
	for (const MenuEntry &m : menu) {
		tomledit::Doc::Table t;
		t.Set("identifier", tomledit::Quote(m.identifier.empty() ? util::Slugify(m.name) : m.identifier));
		t.Set("name", tomledit::Quote(m.name));
		t.Set("url", tomledit::Quote(m.url));
		char buf[32];
		snprintf(buf, sizeof buf, "%d", (int)m.weight);
		t.Set("weight", buf);
		tables.push_back(t);
	}
	a.cfg.SetTables("menu.main", tables);
	a.cfg_dirty = true;
}

struct Pair {
	std::string a, b;
};

std::vector<Pair> ReadPairs(const App &app, const char *prefix, const char *ka, const char *kb) {
	std::vector<Pair> out;
	for (const auto &t : app.cfg.GetTables(prefix)) out.push_back({t.GetString(ka), t.GetString(kb)});
	return out;
}

void WritePairs(App &app, const char *prefix, const char *ka, const char *kb, const std::vector<Pair> &v) {
	std::vector<tomledit::Doc::Table> tables;
	for (const Pair &p : v) {
		tomledit::Doc::Table t;
		t.Set(ka, tomledit::Quote(p.a));
		t.Set(kb, tomledit::Quote(p.b));
		tables.push_back(t);
	}
	app.cfg.SetTables(prefix, tables);
	app.cfg_dirty = true;
}

// A clickable piece of the mock: draws `text` in the given face/colour and
// reports a click, with a selection outline when it is the current target.
bool Hot(const char *id, const char *text, ImFont *face, float px, ImU32 col, Sel sel, int idx = -1) {
	ImGui::PushID(id);
	ImGui::PushFont(face, px);
	ImGui::PushStyleColor(ImGuiCol_Text, Theme::V4(col));
	ImGui::TextUnformatted(text);
	ImGui::PopStyleColor();
	ImGui::PopFont();
	bool clicked = ImGui::IsItemClicked();
	ImVec2 mn = ImGui::GetItemRectMin(), mx = ImGui::GetItemRectMax();
	bool is_sel = (g_sel == sel && g_sel_idx == idx);
	if (ImGui::IsItemHovered()) {
		ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
		ImGui::GetWindowDrawList()->AddRect(ImVec2(mn.x - 4, mn.y - 2), ImVec2(mx.x + 4, mx.y + 2),
		                                    Theme::ACCENT_DIM, 4.0f);
	}
	if (is_sel)
		ImGui::GetWindowDrawList()->AddRect(ImVec2(mn.x - 4, mn.y - 2), ImVec2(mx.x + 4, mx.y + 2), Theme::ACCENT,
		                                    4.0f, 0, 1.6f);
	if (clicked) {
		g_sel = sel;
		g_sel_idx = idx;
	}
	ImGui::PopID();
	return clicked;
}

// ---------------------------------------------------------------- the mock

void DrawMock(App &a, const ImVec2 &size) {
	Paper::Palette pal = Paper::Get(a.design_dark);
	ImGui::PushStyleColor(ImGuiCol_ChildBg, Theme::V4(pal.theme));
	ImGui::BeginChild("mock", size, ImGuiChildFlags_Borders);
	float W = ImGui::GetContentRegionAvail().x;
	float nav = W > 1040 ? 1000 : W - 30;
	float pad = (W - nav) * 0.5f;
	ImGui::Indent(pad);

	// ---- header
	ImGui::Dummy(ImVec2(0, 14));
	std::string title = a.cfg.GetString("title", "site title");
	Hot("sitetitle", title.c_str(), Theme::F_BOLD, Theme::UI_PX * 1.28f, pal.primary, Sel::Title);

	std::vector<MenuEntry> menu = ReadMenu(a);
	{
		// Menu items sit on the header's right, in weight order.
		float total = 0;
		ImGui::PushFont(Theme::F_UI, Theme::UI_PX * 0.95f);
		for (const MenuEntry &m : menu) total += ImGui::CalcTextSize(m.name.c_str()).x + 18;
		total += 30; // the theme toggle
		ImGui::PopFont();
		ImGui::SameLine(pad + nav - total);
		for (size_t i = 0; i < menu.size(); i++) {
			Hot(("menu" + std::to_string(i)).c_str(), menu[i].name.c_str(), Theme::F_UI, Theme::UI_PX * 0.95f,
			    pal.primary, Sel::Menu, (int)i);
			ImGui::SameLine(0, 18);
		}
		if (!a.cfg.GetBool("params.disableThemeToggle", false)) {
			// Drawn rather than typed: the sun/moon code points are outside
			// what the UI font carries, and a missing glyph renders as a
			// tofu box, which looks like a bug in the mock rather than a
			// gap in the font.
			ImVec2 c = ImGui::GetCursorScreenPos();
			float r = Theme::UI_PX * 0.42f;
			ImGui::InvisibleButton("themetoggle", ImVec2(r * 2.4f, r * 2.4f));
			ImDrawList *dl = ImGui::GetWindowDrawList();
			ImVec2 mid(c.x + r * 1.2f, c.y + r * 1.2f);
			if (a.design_dark) {
				dl->AddCircleFilled(mid, r, pal.secondary, 24);
				dl->AddCircleFilled(ImVec2(mid.x + r * 0.55f, mid.y - r * 0.4f), r * 0.85f, pal.theme, 24);
			} else {
				dl->AddCircleFilled(mid, r * 0.6f, pal.secondary, 24);
				for (int k = 0; k < 8; k++) {
					float ang = (float)k * 0.7853981f;
					dl->AddLine(ImVec2(mid.x + cosf(ang) * r * 0.85f, mid.y + sinf(ang) * r * 0.85f),
					            ImVec2(mid.x + cosf(ang) * r * 1.25f, mid.y + sinf(ang) * r * 1.25f),
					            pal.secondary, 1.4f);
				}
			}
			if (ImGui::IsItemClicked()) a.design_dark = !a.design_dark;
			ImGui::SetItemTooltip("the theme toggle PaperMod puts here -- click to flip this preview");
		} else {
			ImGui::NewLine();
		}
	}
	ImGui::Dummy(ImVec2(0, 10));

	if (a.design_page == 0) {
		// ---- home: profile card or text intro
		bool profile = a.cfg.GetBool("params.profileMode.enabled", false);
		ImGui::Dummy(ImVec2(0, 40));
		if (profile) {
			std::string ptitle = a.cfg.GetString("params.profileMode.title", "");
			std::string psub = a.cfg.GetString("params.profileMode.subtitle", "");
			std::string pimg = a.cfg.GetString("params.profileMode.imageUrl", "");
			float img_w = (float)a.cfg.GetNum("params.profileMode.imageWidth", 120);

			// Avatar: a plate, since nothing here decodes images. It says
			// whether the file is actually on disk, which is the thing that
			// goes wrong.
			float r = img_w * 0.5f;
			if (r < 30) r = 30;
			if (r > 80) r = 80;
			ImGui::SetCursorPosX(pad + nav * 0.5f - r);
			ImVec2 c = ImGui::GetCursorScreenPos();
			bool set = !pimg.empty();
			bool have = set && (util::Exists(a.site.root + "/assets/" + pimg) ||
			                    util::Exists(a.site.root + "/static/" + pimg));
			ImGui::InvisibleButton("avatar", ImVec2(r * 2, r * 2));
			if (ImGui::IsItemClicked()) { g_sel = Sel::Profile; g_sel_idx = -1; }
			ImDrawList *dl = ImGui::GetWindowDrawList();
			dl->AddCircleFilled(ImVec2(c.x + r, c.y + r), r, pal.entry, 48);
			// Red means "you pointed at a file that isn't there". No avatar
			// at all is a choice, not a fault, so it stays neutral.
			ImU32 ring = (!set || have) ? pal.tertiary : IM_COL32(0xc0, 0x50, 0x50, 0xff);
			dl->AddCircle(ImVec2(c.x + r, c.y + r), r, ring, 48, 1.5f);
			ImGui::PushFont(Theme::F_UI, Theme::UI_PX * 0.72f);
			const char *lbl = pimg.empty() ? "no avatar set" : (have ? "avatar" : "file not found");
			ImVec2 ts = ImGui::CalcTextSize(lbl);
			dl->AddText(ImVec2(c.x + r - ts.x * 0.5f, c.y + r - ts.y * 0.5f), pal.secondary, lbl);
			ImGui::PopFont();
			ImGui::Dummy(ImVec2(0, 10));

			ImGui::PushFont(Theme::F_BOLD, Theme::UI_PX * 1.7f);
			float tw = ImGui::CalcTextSize(ptitle.c_str()).x;
			ImGui::PopFont();
			ImGui::SetCursorPosX(pad + nav * 0.5f - tw * 0.5f);
			Hot("ptitle", ptitle.c_str(), Theme::F_BOLD, Theme::UI_PX * 1.7f, pal.primary, Sel::Profile);

			ImGui::PushFont(Theme::F_UI, Theme::UI_PX);
			float sw = ImGui::CalcTextSize(psub.c_str()).x;
			ImGui::PopFont();
			ImGui::SetCursorPosX(pad + nav * 0.5f - sw * 0.5f);
			Hot("psub", psub.c_str(), Theme::F_UI, Theme::UI_PX, pal.secondary, Sel::Profile);

			ImGui::Dummy(ImVec2(0, 14));
			std::vector<Pair> buttons = ReadPairs(a, "params.profileMode.buttons", "name", "url");
			float bw = 0;
			ImGui::PushFont(Theme::F_UI, Theme::UI_PX * 0.95f);
			for (const Pair &b : buttons) bw += ImGui::CalcTextSize(b.a.c_str()).x + 34;
			ImGui::PopFont();
			ImGui::SetCursorPosX(pad + nav * 0.5f - bw * 0.5f);
			for (size_t i = 0; i < buttons.size(); i++) {
				ImGui::PushID((int)i);
				ImGui::PushStyleColor(ImGuiCol_Button, Theme::V4(pal.entry));
				ImGui::PushStyleColor(ImGuiCol_Text, Theme::V4(pal.primary));
				if (ImGui::Button(buttons[i].a.c_str())) { g_sel = Sel::Buttons; g_sel_idx = (int)i; }
				ImGui::PopStyleColor(2);
				if (g_sel == Sel::Buttons && g_sel_idx == (int)i)
					ImGui::GetWindowDrawList()->AddRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(),
					                                    Theme::ACCENT, 5.0f, 0, 1.6f);
				ImGui::PopID();
				ImGui::SameLine(0, 10);
			}
			ImGui::NewLine();

			ImGui::Dummy(ImVec2(0, 10));
			std::vector<Pair> social = ReadPairs(a, "params.socialIcons", "name", "url");
			float sww = 0;
			ImGui::PushFont(Theme::F_UI, Theme::UI_PX * 0.9f);
			for (const Pair &s : social) sww += ImGui::CalcTextSize(s.a.c_str()).x + 22;
			ImGui::PopFont();
			ImGui::SetCursorPosX(pad + nav * 0.5f - sww * 0.5f);
			for (size_t i = 0; i < social.size(); i++) {
				Hot(("soc" + std::to_string(i)).c_str(), social[i].a.c_str(), Theme::F_UI, Theme::UI_PX * 0.9f,
				    pal.secondary, Sel::Social, (int)i);
				ImGui::SameLine(0, 22);
			}
			ImGui::NewLine();
		} else {
			std::string ht = a.cfg.GetString("params.homeInfoParams.Title", "(homeInfoParams.Title)");
			std::string hc = a.cfg.GetString("params.homeInfoParams.Content", "");
			Hot("hitle", ht.c_str(), Theme::F_BOLD, Theme::UI_PX * 1.6f, pal.primary, Sel::Profile);
			ImGui::PushTextWrapPos(pad + nav);
			md::RenderStyle st = a.PreviewStyle(a.design_dark);
			md::Render(md::ParseBlocks(hc), st);
			ImGui::PopTextWrapPos();
			ImGui::PushStyleColor(ImGuiCol_Text, Theme::V4(pal.secondary));
			ImGui::PushFont(Theme::F_UI, Theme::UI_PX * 0.85f);
			ImGui::TextUnformatted("(text intro mode -- the recent-posts list follows underneath)");
			ImGui::PopFont();
			ImGui::PopStyleColor();
		}
	} else {
		// ---- post list, built from the real content tree
		ImGui::Dummy(ImVec2(0, 16));
		ImGui::PushTextWrapPos(pad + nav);
		int shown = 0;
		int per_page = (int)a.cfg.GetNum("pagination.pagerSize", 10);
		bool build_drafts = a.cfg.GetBool("buildDrafts", false);
		for (const site::Post &p : a.site.posts) {
			if (p.is_branch) continue;
			// Root-level pages (about, search) are not posts -- they have
			// their own URLs and never appear in a section listing.
			if (p.section.empty()) continue;
			if (p.draft && !build_drafts) continue;
			if (shown++ >= per_page) break;
			ImVec2 c = ImGui::GetCursorScreenPos();
			float h = 84;
			ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(c.x - 14, c.y - 8),
			                                          ImVec2(c.x + nav + 14, c.y + h), pal.entry, 8.0f);
			ImGui::PushFont(Theme::F_BOLD, Theme::UI_PX * 1.25f);
			ImGui::PushStyleColor(ImGuiCol_Text, Theme::V4(pal.primary));
			ImGui::TextUnformatted(p.title.c_str());
			ImGui::PopStyleColor();
			ImGui::PopFont();

			std::string meta;
			if (!a.cfg.GetBool("params.hidemeta", false)) {
				if (!p.date.empty()) meta = util::PrettyDate(p.date).substr(0, 10);
				if (a.cfg.GetBool("params.ShowReadingTime", false))
					meta += (meta.empty() ? "" : " · ") + std::to_string(p.words / 200 + 1) + " min";
				if (a.cfg.GetBool("params.ShowWordCount", false))
					meta += (meta.empty() ? "" : " · ") + std::to_string(p.words) + " words";
			}
			Hot(("meta" + p.rel).c_str(), meta.empty() ? " " : meta.c_str(), Theme::F_UI, Theme::UI_PX * 0.8f,
			    pal.secondary, Sel::PostMeta);
			ImGui::PushFont(Theme::F_UI, Theme::UI_PX * 0.92f);
			ImGui::PushStyleColor(ImGuiCol_Text, Theme::V4(pal.content));
			std::string sum = p.summary.empty() ? "(Hugo will use the opening words of the post here)"
			                                    : p.summary;
			ImGui::TextWrapped("%s", sum.c_str());
			ImGui::PopStyleColor();
			ImGui::PopFont();
			ImGui::SetCursorScreenPos(ImVec2(c.x, c.y + h + 12));
		}
		if (shown == 0) {
			ImGui::PushStyleColor(ImGuiCol_Text, Theme::V4(pal.secondary));
			ImGui::TextUnformatted(build_drafts
			                           ? "No posts in any section yet."
			                           : "No published posts in any section. Drafts are hidden here because\n"
			                             "buildDrafts is off -- the local preview server shows them anyway.");
			ImGui::PopStyleColor();
		}
		ImGui::PopTextWrapPos();
	}

	// ---- footer
	ImGui::Dummy(ImVec2(0, 30));
	ImGui::PushStyleColor(ImGuiCol_Text, Theme::V4(pal.secondary));
	ImGui::PushFont(Theme::F_UI, Theme::UI_PX * 0.8f);
	std::string foot = "© 2026 " + a.cfg.GetString("title", "") + " · Powered by Hugo & PaperMod";
	float fw = ImGui::CalcTextSize(foot.c_str()).x;
	ImGui::PopFont();
	ImGui::SetCursorPosX(pad + nav * 0.5f - fw * 0.5f);
	ImGui::PushFont(Theme::F_UI, Theme::UI_PX * 0.8f);
	ImGui::TextUnformatted(foot.c_str());
	ImGui::PopFont();
	ImGui::PopStyleColor();
	ImGui::Dummy(ImVec2(0, 20));

	ImGui::Unindent(pad);
	ImGui::EndChild();
	ImGui::PopStyleColor();
}

// ----------------------------------------------------------- the inspector

void StringRow(App &a, const char *label, const char *path, const char *hint = nullptr,
               const char *help = nullptr) {
	std::string v = a.cfg.GetString(path, "");
	W::LabelRow(label);
	ImGui::PushID(path);
	if (ImGui::InputTextWithHint("##v", hint ? hint : "", &v)) {
		a.cfg.SetString(path, v);
		a.cfg_dirty = true;
	}
	ImGui::PopID();
	if (help) {
		ImGui::PushStyleColor(ImGuiCol_Text, Theme::V4(Theme::TEXT_FAINT));
		ImGui::PushFont(Theme::F_UI, Theme::UI_PX * 0.8f);
		ImGui::Indent(150);
		ImGui::TextWrapped("%s", help);
		ImGui::Unindent(150);
		ImGui::PopFont();
		ImGui::PopStyleColor();
	}
}

void BoolRow(App &a, const char *label, const char *path, bool def, const char *help) {
	bool v = a.cfg.GetBool(path, def);
	ImGui::PushID(path);
	if (W::ToggleRow(label, &v, help)) {
		a.cfg.SetBool(path, v);
		a.cfg_dirty = true;
	}
	ImGui::PopID();
}

void NumRow(App &a, const char *label, const char *path, double def, const char *help) {
	int v = (int)a.cfg.GetNum(path, def);
	W::LabelRow(label);
	ImGui::PushID(path);
	ImGui::SetNextItemWidth(120);
	if (ImGui::InputInt("##v", &v)) {
		if (v < 1) v = 1;
		a.cfg.SetNum(path, v);
		a.cfg_dirty = true;
	}
	ImGui::PopID();
	if (help) {
		ImGui::PushStyleColor(ImGuiCol_Text, Theme::V4(Theme::TEXT_FAINT));
		ImGui::PushFont(Theme::F_UI, Theme::UI_PX * 0.8f);
		ImGui::Indent(150);
		ImGui::TextWrapped("%s", help);
		ImGui::Unindent(150);
		ImGui::PopFont();
		ImGui::PopStyleColor();
	}
}

void MenuEditor(App &a) {
	std::vector<MenuEntry> menu = ReadMenu(a);
	bool changed = false;
	int remove_at = -1, move_from = -1, move_to = -1;

	for (size_t i = 0; i < menu.size(); i++) {
		ImGui::PushID((int)i);
		bool sel = (g_sel == Sel::Menu && g_sel_idx == (int)i);
		if (sel) ImGui::PushStyleColor(ImGuiCol_ChildBg, Theme::V4(Theme::PANEL_HI));
		ImGui::BeginChild("row", ImVec2(0, 96), ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY);
		W::LabelRow("Label", 70);
		if (ImGui::InputText("##name", &menu[i].name)) changed = true;
		W::LabelRow("URL", 70);
		if (ImGui::InputText("##url", &menu[i].url)) changed = true;
		int w = (int)menu[i].weight;
		W::LabelRow("Order", 70);
		ImGui::SetNextItemWidth(90);
		if (ImGui::InputInt("##weight", &w, 10)) {
			menu[i].weight = w;
			changed = true;
		}
		ImGui::SameLine();
		if (ImGui::SmallButton("up") && i > 0) { move_from = (int)i; move_to = (int)i - 1; }
		ImGui::SameLine();
		if (ImGui::SmallButton("down") && i + 1 < menu.size()) { move_from = (int)i; move_to = (int)i + 1; }
		ImGui::SameLine();
		if (ImGui::SmallButton("remove")) remove_at = (int)i;
		ImGui::EndChild();
		if (ImGui::IsItemClicked()) { g_sel = Sel::Menu; g_sel_idx = (int)i; }
		if (sel) ImGui::PopStyleColor();
		ImGui::PopID();
	}

	if (move_from >= 0) {
		// Reordering means swapping the weights, because weight is what Hugo
		// actually sorts on -- moving the table rows alone would look right
		// here and change nothing on the site.
		std::swap(menu[move_from].weight, menu[move_to].weight);
		changed = true;
	}
	if (remove_at >= 0) {
		menu.erase(menu.begin() + remove_at);
		changed = true;
		g_sel_idx = -1;
	}
	if (ImGui::Button("+ Add menu item")) {
		MenuEntry m;
		m.name = "New";
		m.url = "/";
		m.weight = menu.empty() ? 10 : menu.back().weight + 10;
		menu.push_back(m);
		changed = true;
	}
	ImGui::SameLine();
	if (ImGui::BeginCombo("##addsection", "add a section", ImGuiComboFlags_WidthFitPreview)) {
		for (const std::string &s : a.site.sections) {
			if (ImGui::Selectable(s.c_str())) {
				MenuEntry m;
				m.name = (char)toupper(s[0]) + s.substr(1);
				m.url = "/" + s + "/";
				m.weight = menu.empty() ? 10 : menu.back().weight + 10;
				menu.push_back(m);
				changed = true;
			}
		}
		ImGui::EndCombo();
	}
	if (changed) WriteMenu(a, menu);
}

void PairListEditor(App &a, const char *prefix, const char *ka, const char *kb, const char *label_a,
                    const char *label_b, const std::vector<const char *> &suggestions, Sel sel_kind) {
	std::vector<Pair> items = ReadPairs(a, prefix, ka, kb);
	bool changed = false;
	int remove_at = -1;
	for (size_t i = 0; i < items.size(); i++) {
		ImGui::PushID((int)i);
		bool sel = (g_sel == sel_kind && g_sel_idx == (int)i);
		if (sel) ImGui::PushStyleColor(ImGuiCol_ChildBg, Theme::V4(Theme::PANEL_HI));
		ImGui::BeginChild("row", ImVec2(0, 0), ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY);
		W::LabelRow(label_a, 70);
		if (ImGui::InputText("##a", &items[i].a)) changed = true;
		if (!suggestions.empty()) {
			ImGui::SameLine();
			if (ImGui::BeginCombo("##pick", "", ImGuiComboFlags_NoPreview)) {
				for (const char *s : suggestions)
					if (ImGui::Selectable(s)) {
						items[i].a = s;
						changed = true;
					}
				ImGui::EndCombo();
			}
		}
		W::LabelRow(label_b, 70);
		if (ImGui::InputText("##b", &items[i].b)) changed = true;
		if (ImGui::SmallButton("remove")) remove_at = (int)i;
		ImGui::EndChild();
		if (ImGui::IsItemClicked()) { g_sel = sel_kind; g_sel_idx = (int)i; }
		if (sel) ImGui::PopStyleColor();
		ImGui::PopID();
	}
	if (remove_at >= 0) {
		items.erase(items.begin() + remove_at);
		changed = true;
	}
	if (ImGui::Button("+ Add")) {
		items.push_back({suggestions.empty() ? "name" : suggestions[0], ""});
		changed = true;
	}
	if (changed) WritePairs(a, prefix, ka, kb, items);
}

void Inspector(App &a, const ImVec2 &size) {
	ImGui::BeginChild("inspector", size, ImGuiChildFlags_Borders);

	ImGui::PushFont(Theme::F_BOLD, Theme::UI_PX * 1.05f);
	ImGui::TextUnformatted("Site design");
	ImGui::PopFont();
	ImGui::PushStyleColor(ImGuiCol_Text, Theme::V4(Theme::TEXT_FAINT));
	ImGui::TextWrapped("Everything here is written to hugo.toml, one line at a time -- the file's comments "
	                   "and ordering survive.");
	ImGui::PopStyleColor();
	ImGui::Dummy(ImVec2(0, 6));

	if (a.cfg_dirty) {
		if (W::PrimaryButton("Save hugo.toml", ImVec2(150, 0))) a.SaveConfig();
		ImGui::SameLine();
		if (ImGui::Button("Revert")) {
			a.cfg.SetText(a.cfg_text_on_disk);
			a.cfg_dirty = false;
			a.Notify("reverted hugo.toml");
		}
		ImGui::SameLine();
		ImGui::PushStyleColor(ImGuiCol_Text, Theme::V4(Theme::WARN));
		ImGui::AlignTextToFramePadding();
		ImGui::TextUnformatted("unsaved");
		ImGui::PopStyleColor();
		ImGui::Dummy(ImVec2(0, 4));
	}

	auto open_if = [&](Sel s) { if (g_sel == s) ImGui::SetNextItemOpen(true, ImGuiCond_Always); };

	open_if(Sel::Title);
	if (ImGui::CollapsingHeader("Identity", ImGuiTreeNodeFlags_DefaultOpen)) {
		StringRow(a, "Site title", "title", "camheard.us", "Shown in the header and in the browser tab.");
		StringRow(a, "Base URL", "baseURL", "https://example.com/",
		          "Where the built site will live. Every absolute link is built from this.");
		StringRow(a, "Description", "params.description", "",
		          "The meta description search engines and link previews use.");
		StringRow(a, "Author", "params.author", "");
		StringRow(a, "Language", "locale", "en-us");
	}

	open_if(Sel::Profile);
	if (g_sel == Sel::Buttons) ImGui::SetNextItemOpen(true, ImGuiCond_Always);
	if (ImGui::CollapsingHeader("Home page")) {
		bool profile = a.cfg.GetBool("params.profileMode.enabled", false);
		ImGui::TextUnformatted("Layout");
		if (ImGui::RadioButton("Profile card", profile)) {
			a.cfg.SetBool("params.profileMode.enabled", true);
			a.cfg_dirty = true;
		}
		ImGui::SameLine();
		if (ImGui::RadioButton("Text intro", !profile)) {
			a.cfg.SetBool("params.profileMode.enabled", false);
			a.cfg_dirty = true;
		}
		ImGui::PushStyleColor(ImGuiCol_Text, Theme::V4(Theme::TEXT_FAINT));
		ImGui::TextWrapped("The profile card centres an avatar, a name and some buttons. The text intro puts "
		                   "a headline and a paragraph above the recent-posts list.");
		ImGui::PopStyleColor();
		ImGui::Dummy(ImVec2(0, 6));

		if (profile) {
			StringRow(a, "Name", "params.profileMode.title");
			StringRow(a, "Subtitle", "params.profileMode.subtitle");
			StringRow(a, "Avatar", "params.profileMode.imageUrl", "img/avatar.png",
			          "Relative to assets/ (Hugo Pipes) or static/. Leave empty for no avatar.");
			NumRow(a, "Avatar size", "params.profileMode.imageWidth", 120, nullptr);
			W::SectionHeader("Buttons");
			PairListEditor(a, "params.profileMode.buttons", "name", "url", "Label", "URL", {}, Sel::Buttons);
		} else {
			StringRow(a, "Headline", "params.homeInfoParams.Title");
			std::string content = a.cfg.GetString("params.homeInfoParams.Content", "");
			W::LabelRow("Intro text");
			if (ImGui::InputTextMultiline("##hic", &content, ImVec2(-1, 90))) {
				a.cfg.SetString("params.homeInfoParams.Content", content);
				a.cfg_dirty = true;
			}
			ImGui::PushStyleColor(ImGuiCol_Text, Theme::V4(Theme::TEXT_FAINT));
			ImGui::TextWrapped("Markdown works here; it renders in the mock on the left.");
			ImGui::PopStyleColor();
		}
	}

	open_if(Sel::Menu);
	if (ImGui::CollapsingHeader("Header menu")) {
		ImGui::PushStyleColor(ImGuiCol_Text, Theme::V4(Theme::TEXT_FAINT));
		ImGui::TextWrapped("The links across the top. Order is the weight: lower comes first.");
		ImGui::PopStyleColor();
		MenuEditor(a);
	}

	open_if(Sel::Social);
	if (ImGui::CollapsingHeader("Social icons")) {
		ImGui::PushStyleColor(ImGuiCol_Text, Theme::V4(Theme::TEXT_FAINT));
		ImGui::TextWrapped("PaperMod draws an icon per name it recognises; an unknown name gets no icon, so "
		                   "the spelling matters.");
		ImGui::PopStyleColor();
		PairListEditor(a, "params.socialIcons", "name", "url", "Network", "URL",
		               {"github", "gitlab", "rss", "email", "mastodon", "linkedin", "x", "bluesky", "youtube",
		                "stackoverflow", "reddit", "discord", "telegram", "kofi"},
		               Sel::Social);
	}

	if (ImGui::CollapsingHeader("Appearance")) {
		const char *themes[] = {"dark", "light", "auto"};
		std::string cur = a.cfg.GetString("params.defaultTheme", "auto");
		W::LabelRow("Default theme");
		if (ImGui::BeginCombo("##dt", cur.c_str())) {
			for (const char *t : themes)
				if (ImGui::Selectable(t, cur == t)) {
					a.cfg.SetString("params.defaultTheme", t);
					a.cfg_dirty = true;
				}
			ImGui::EndCombo();
		}
		BoolRow(a, "Hide the theme toggle", "params.disableThemeToggle", false,
		        "Removes the sun/moon switch from the header; visitors are stuck with the default.");
		BoolRow(a, "Syntax highlighting without highlight.js", "params.assets.disableHLJS", true,
		        "On = Hugo's own Chroma highlighter is used (what this site does). Off = ship highlight.js.");
		StringRow(a, "Favicon", "params.assets.favicon", "favicon.ico");
	}

	open_if(Sel::PostMeta);
	if (ImGui::CollapsingHeader("Post options")) {
		ImGui::PushStyleColor(ImGuiCol_Text, Theme::V4(Theme::TEXT_FAINT));
		ImGui::TextWrapped("Defaults for every post. Any post can override them in its own front matter.");
		ImGui::PopStyleColor();
		BoolRow(a, "Reading time", "params.ShowReadingTime", false, nullptr);
		BoolRow(a, "Word count", "params.ShowWordCount", false, nullptr);
		BoolRow(a, "Table of contents", "params.ShowToc", false, nullptr);
		BoolRow(a, "Table of contents starts open", "params.TocOpen", false, nullptr);
		BoolRow(a, "Breadcrumbs", "params.ShowBreadCrumbs", false, nullptr);
		BoolRow(a, "Previous/next links", "params.ShowPostNavLinks", false, nullptr);
		BoolRow(a, "Share buttons", "params.ShowShareButtons", false, nullptr);
		BoolRow(a, "Copy button on code blocks", "params.ShowCodeCopyButtons", false, nullptr);
		BoolRow(a, "Hide date and reading time", "params.hidemeta", false, nullptr);
		BoolRow(a, "Comments", "params.comments", false,
		        "PaperMod only reserves the slot; the comment system itself is a partial you provide.");
	}

	if (ImGui::CollapsingHeader("Build")) {
		NumRow(a, "Posts per page", "pagination.pagerSize", 10, nullptr);
		BoolRow(a, "Include drafts in a build", "buildDrafts", false,
		        "Off is the safe default: drafts stay local. The preview server shows them regardless.");
		BoolRow(a, "Include future-dated posts", "buildFuture", false, nullptr);
		BoolRow(a, "Include expired posts", "buildExpired", false, nullptr);
		BoolRow(a, "Write robots.txt", "enableRobotsTXT", false, nullptr);
		BoolRow(a, "Minify output", "minify.minifyOutput", false, nullptr);
	}

	if (ImGui::CollapsingHeader("hugo.toml (read only)")) {
		ImGui::PushStyleColor(ImGuiCol_Text, Theme::V4(Theme::TEXT_FAINT));
		ImGui::TextWrapped("Exactly what will be written. Edits above change single lines here.");
		ImGui::PopStyleColor();
		std::string text = a.cfg.Text();
		ImGui::PushFont(Theme::F_MONO, Theme::UI_PX * 0.8f);
		ImGui::InputTextMultiline("##raw", &text, ImVec2(-1, 300), ImGuiInputTextFlags_ReadOnly);
		ImGui::PopFont();
	}

	ImGui::Dummy(ImVec2(0, 20));
	ImGui::EndChild();
}

} // namespace

void DrawDesignTab(App &a) {
	const char *pages[] = {"Home page", "Post list"};
	for (int i = 0; i < 2; i++) {
		if (W::ToolButton(pages[i], nullptr, a.design_page == i)) a.design_page = i;
		ImGui::SameLine(0, 4);
	}
	ImGui::SameLine(0, 16);
	if (W::ToolButton(a.design_dark ? "dark theme" : "light theme", "flip the mock between the site's two themes"))
		a.design_dark = !a.design_dark;
	ImGui::SameLine(0, 16);
	ImGui::PushStyleColor(ImGuiCol_Text, Theme::V4(Theme::TEXT_FAINT));
	ImGui::AlignTextToFramePadding();
	ImGui::TextUnformatted("click anything in the mock to jump to the setting behind it");
	ImGui::PopStyleColor();

	ImGui::Dummy(ImVec2(0, 4));
	float insp_w = 430;
	ImVec2 avail = ImGui::GetContentRegionAvail();
	DrawMock(a, ImVec2(avail.x - insp_w - 10, avail.y));
	ImGui::SameLine(0, 10);
	Inspector(a, ImVec2(insp_w, avail.y));
}
