// Media tab -- what is in static/ and assets/, and how a post refers to it.
#include "app.h"
#include "util.h"
#include "widgets.h"

#include "imgui.h"
#include "imgui_stdlib.h"

#include <unistd.h>

void DrawMediaTab(App &a) {
	ImGui::PushFont(Theme::F_BOLD, Theme::UI_PX * 1.05f);
	ImGui::TextUnformatted("Files the site ships");
	ImGui::PopFont();
	ImGui::PushStyleColor(ImGuiCol_Text, Theme::V4(Theme::TEXT_FAINT));
	ImGui::TextWrapped(
	    "static/ is copied to the site root as-is, so static/img/a.png is served at /img/a.png. assets/ goes "
	    "through Hugo Pipes instead and has no direct URL -- the theme reads it (the avatar lives there). "
	    "Drag a file onto this window to copy it into static/img.");
	ImGui::PopStyleColor();
	ImGui::Dummy(ImVec2(0, 6));

	ImGui::SetNextItemWidth(260);
	ImGui::InputTextWithHint("##mfilter", "filter", &a.media_filter);
	ImGui::SameLine();
	if (ImGui::Button("Rescan")) {
		a.site.ScanMedia();
		a.Notify("rescanned static/ and assets/");
	}

	ImGui::Dummy(ImVec2(0, 4));
	float detail_w = 344;
	ImVec2 avail = ImGui::GetContentRegionAvail();
	ImGui::BeginChild("mlist", ImVec2(avail.x - detail_w, 0), ImGuiChildFlags_None);
	if (ImGui::BeginTable("media", 4,
	                      ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_ScrollY |
	                          ImGuiTableFlags_SizingStretchProp)) {
		ImGui::TableSetupScrollFreeze(0, 1);
		ImGui::TableSetupColumn("File", ImGuiTableColumnFlags_WidthStretch, 2.4f);
		ImGui::TableSetupColumn("Served at", ImGuiTableColumnFlags_WidthStretch, 2.0f);
		ImGui::TableSetupColumn("Kind", ImGuiTableColumnFlags_WidthStretch, 0.8f);
		ImGui::TableSetupColumn("Size", ImGuiTableColumnFlags_WidthStretch, 0.8f);
		ImGui::TableHeadersRow();
		for (int i = 0; i < (int)a.site.media.size(); i++) {
			const site::MediaFile &m = a.site.media[i];
			if (!a.media_filter.empty() && !util::IContains(m.rel, a.media_filter)) continue;
			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);
			ImGui::PushID(i);
			if (ImGui::Selectable(m.rel.c_str(), a.media_selected == i, ImGuiSelectableFlags_SpanAllColumns))
				a.media_selected = i;
			ImGui::PopID();
			ImGui::PushStyleColor(ImGuiCol_Text, Theme::V4(Theme::TEXT_DIM));
			ImGui::TableSetColumnIndex(1);
			ImGui::TextUnformatted(util::StartsWith(m.rel, "static/") ? m.web.c_str() : "(via Hugo Pipes)");
			ImGui::TableSetColumnIndex(2);
			ImGui::TextUnformatted(m.image ? "image" : "file");
			ImGui::TableSetColumnIndex(3);
			ImGui::TextUnformatted(util::HumanSize(m.size).c_str());
			ImGui::PopStyleColor();
		}
		ImGui::EndTable();
	}
	if (a.site.media.empty()) {
		ImGui::PushStyleColor(ImGuiCol_Text, Theme::V4(Theme::TEXT_FAINT));
		ImGui::TextWrapped("Nothing in static/ or assets/ yet. Drop a file onto this window to add one.");
		ImGui::PopStyleColor();
	}
	ImGui::EndChild();

	ImGui::SameLine();
	ImGui::BeginChild("mdetail", ImVec2(0, 0), ImGuiChildFlags_Borders);
	if (a.media_selected < 0 || a.media_selected >= (int)a.site.media.size()) {
		ImGui::PushStyleColor(ImGuiCol_Text, Theme::V4(Theme::TEXT_FAINT));
		ImGui::TextWrapped("Select a file to get its reference, or to remove it.");
		ImGui::PopStyleColor();
		ImGui::EndChild();
		return;
	}
	const site::MediaFile &m = a.site.media[a.media_selected];
	ImGui::PushFont(Theme::F_BOLD, Theme::UI_PX);
	ImGui::TextWrapped("%s", util::BaseName(m.rel).c_str());
	ImGui::PopFont();

	W::SectionHeader("Path");
	ImGui::PushFont(Theme::F_MONO, Theme::UI_PX * 0.82f);
	ImGui::PushStyleColor(ImGuiCol_Text, Theme::V4(Theme::TEXT_DIM));
	ImGui::TextWrapped("%s", m.rel.c_str());
	ImGui::PopStyleColor();
	ImGui::PopFont();

	bool is_static = util::StartsWith(m.rel, "static/");
	W::SectionHeader("Reference it with");
	std::string snippet;
	if (is_static) snippet = m.image ? "![alt text](" + m.web + ")" : "[" + util::BaseName(m.rel) + "](" + m.web + ")";
	else snippet = "assets: " + m.web;
	ImGui::PushFont(Theme::F_MONO, Theme::UI_PX * 0.82f);
	ImGui::PushStyleColor(ImGuiCol_Text, Theme::V4(Theme::TEXT));
	ImGui::TextWrapped("%s", snippet.c_str());
	ImGui::PopStyleColor();
	ImGui::PopFont();
	if (!is_static) {
		ImGui::PushStyleColor(ImGuiCol_Text, Theme::V4(Theme::TEXT_FAINT));
		ImGui::TextWrapped("Files under assets/ are not served directly -- a theme or shortcode has to pull "
		                   "them in. The PaperMod avatar (params.profileMode.imageUrl) is the one this site "
		                   "uses.");
		ImGui::PopStyleColor();
	}

	W::SectionHeader("Facts");
	ImGui::PushStyleColor(ImGuiCol_Text, Theme::V4(Theme::TEXT_DIM));
	ImGui::Text("%s", util::HumanSize(m.size).c_str());
	ImGui::Text("saved %s", util::PrettyEpoch(util::MTime(m.path)).c_str());
	ImGui::PopStyleColor();

	W::SectionHeader("Actions");
	if (ImGui::Button("Copy markdown", ImVec2(-1, 0))) {
		ImGui::SetClipboardText(snippet.c_str());
		a.Notify("copied");
	}
	ImGui::BeginDisabled(a.ed.path.empty() || !is_static);
	if (ImGui::Button("Insert into the open post", ImVec2(-1, 0))) {
		int sa = a.ed.sel_a, sb = a.ed.sel_b;
		md::InsertText(&a.ed.body, &sa, &sb, snippet + "\n");
		a.ed.sel_a = sa;
		a.ed.sel_b = sb;
		a.ed.dirty = true;
		a.tab = Tab::Editor;
		a.Notify("inserted into " + util::BaseName(a.ed.path));
	}
	ImGui::EndDisabled();
	if (a.ed.path.empty()) ImGui::SetItemTooltip("open a post on the Editor tab first");
	if (ImGui::Button("Open with the desktop", ImVec2(-1, 0))) util::OpenURL(m.path);

	ImGui::Dummy(ImVec2(0, 6));
	std::string path = m.path;
	if (W::DangerButton("Delete file...", ImVec2(-1, 0))) ImGui::OpenPopup("delmedia");
	if (ImGui::BeginPopupModal("delmedia", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
		ImGui::TextWrapped("Delete %s?", util::BaseName(path).c_str());
		ImGui::PushStyleColor(ImGuiCol_Text, Theme::V4(Theme::TEXT_DIM));
		ImGui::TextWrapped("Any post still pointing at it will render a broken image.");
		ImGui::PopStyleColor();
		if (W::DangerButton("Delete", ImVec2(110, 0))) {
			if (unlink(path.c_str()) == 0) {
				a.site.ScanMedia();
				a.media_selected = -1;
				a.Notify("deleted " + util::BaseName(path));
			} else {
				a.Notify("could not delete " + path, true);
			}
			ImGui::CloseCurrentPopup();
		}
		ImGui::SameLine();
		if (ImGui::Button("Cancel", ImVec2(110, 0))) ImGui::CloseCurrentPopup();
		ImGui::EndPopup();
	}
	ImGui::EndChild();
}
