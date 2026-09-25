// Check tab -- the things a Hugo build is perfectly happy about and a reader
// is not.
//
// `hugo` exits 0 on a link to a page that was renamed last month, on an
// image whose file never got committed, and on a static/ directory slowly
// filling up with files nothing points at. None of that is a build error, so
// none of it shows up anywhere until someone clicks the wrong thing on the
// live site. This tab is that list, and every row in it opens the file it is
// about.
//
// The work is all in audit.cpp; this is the table.
#include "app.h"
#include "util.h"
#include "widgets.h"

#include "imgui.h"
#include "imgui_stdlib.h"

namespace {

ImU32 LevelColor(audit::Level l) {
	switch (l) {
	case audit::Level::Error: return Theme::DANGER;
	case audit::Level::Warn: return Theme::WARN;
	default: return Theme::TEXT_FAINT;
	}
}

const char *LevelName(audit::Level l) {
	switch (l) {
	case audit::Level::Error: return "error";
	case audit::Level::Warn: return "warn";
	default: return "note";
	}
}

// A count with a label under it, in the level's colour. Three of these is
// the whole verdict.
void Tile(const char *label, int n, ImU32 col, const char *tip) {
	ImGui::BeginGroup();
	ImGui::PushFont(Theme::F_BOLD, Theme::UI_PX * 1.8f);
	ImGui::PushStyleColor(ImGuiCol_Text, Theme::V4(n ? col : Theme::TEXT_FAINT));
	ImGui::Text("%d", n);
	ImGui::PopStyleColor();
	ImGui::PopFont();
	ImGui::PushFont(Theme::F_UI, Theme::UI_PX * 0.8f);
	ImGui::PushStyleColor(ImGuiCol_Text, Theme::V4(Theme::TEXT_DIM));
	ImGui::TextUnformatted(label);
	ImGui::PopStyleColor();
	ImGui::PopFont();
	ImGui::EndGroup();
	if (tip) ImGui::SetItemTooltip("%s", tip);
}

} // namespace

void DrawCheckTab(App &a) {
	a.EnsureAudit();
	const audit::Report &r = a.report;

	// ---- header
	ImGui::PushFont(Theme::F_BOLD, Theme::UI_PX * 1.05f);
	ImGui::TextUnformatted("What the build won't tell you");
	ImGui::PopFont();
	ImGui::PushStyleColor(ImGuiCol_Text, Theme::V4(Theme::TEXT_FAINT));
	ImGui::TextWrapped(
	    "Every page under content/ read back and cross-referenced with static/, assets/ and hugo.toml. "
	    "Hugo builds all of this without complaint -- a link to a page that no longer exists is a 404 for "
	    "the reader and a successful build for everyone else.");
	ImGui::PopStyleColor();

	ImGui::Dummy(ImVec2(0, 8));
	Tile("errors", r.errors, Theme::DANGER, "broken links and missing images: visibly wrong on the live site");
	ImGui::SameLine(0, 40);
	Tile("warnings", r.warnings, Theme::WARN, "pages that will publish, but not the way you meant");
	ImGui::SameLine(0, 40);
	Tile("notes", r.infos, Theme::TEXT_DIM, "housekeeping: drafts, missing tags, unreferenced files");
	ImGui::SameLine(0, 40);
	ImGui::BeginGroup();
	ImGui::PushStyleColor(ImGuiCol_Text, Theme::V4(Theme::TEXT_DIM));
	ImGui::Text("%d page%s checked", r.pages, r.pages == 1 ? "" : "s");
	ImGui::Text("%d internal link%s, %d image reference%s", r.links, r.links == 1 ? "" : "s", r.images,
	            r.images == 1 ? "" : "s");
	ImGui::PopStyleColor();
	ImGui::EndGroup();

	ImGui::SameLine(ImGui::GetContentRegionMax().x - 130);
	if (W::PrimaryButton("Re-check", ImVec2(130, 0))) {
		a.ReloadAll();
		a.audit_stale = true;
		a.Notify("re-checked the site");
	}
	ImGui::SetItemTooltip("rescans content/ and static/ from disk first");

	// ---- filters
	ImGui::Dummy(ImVec2(0, 6));
	std::vector<std::string> kinds = {"everything"};
	for (int k = 0; k < (int)audit::Kind::COUNT; k++) {
		int n = r.Count((audit::Kind)k);
		kinds.push_back(std::string(audit::KindName((audit::Kind)k)) + "  (" + std::to_string(n) + ")");
	}
	if (a.check_kind_filter >= (int)kinds.size()) a.check_kind_filter = 0;
	ImGui::SetNextItemWidth(240);
	if (ImGui::BeginCombo("##kind", kinds[a.check_kind_filter].c_str())) {
		for (int i = 0; i < (int)kinds.size(); i++)
			if (ImGui::Selectable(kinds[i].c_str(), a.check_kind_filter == i)) a.check_kind_filter = i;
		ImGui::EndCombo();
	}
	ImGui::SameLine();
	ImGui::Checkbox("hide notes", &a.check_hide_info);
	ImGui::SetItemTooltip("leave only the things that are actually wrong");
	if (a.check_kind_filter > 0) {
		ImGui::SameLine(0, 16);
		ImGui::PushStyleColor(ImGuiCol_Text, Theme::V4(Theme::TEXT_FAINT));
		ImGui::AlignTextToFramePadding();
		ImGui::TextWrapped("%s", audit::KindHelp((audit::Kind)(a.check_kind_filter - 1)));
		ImGui::PopStyleColor();
	}

	// ---- the list
	ImGui::Dummy(ImVec2(0, 4));
	ImGuiTableFlags tf = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_ScrollY |
	                     ImGuiTableFlags_SizingStretchProp;
	int shown = 0;
	if (ImGui::BeginTable("findings", 5, tf, ImVec2(0, ImGui::GetContentRegionAvail().y))) {
		ImGui::TableSetupScrollFreeze(0, 1);
		ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 52.0f);
		ImGui::TableSetupColumn("What", ImGuiTableColumnFlags_WidthStretch, 1.1f);
		ImGui::TableSetupColumn("Where", ImGuiTableColumnFlags_WidthStretch, 1.8f);
		ImGui::TableSetupColumn("Subject", ImGuiTableColumnFlags_WidthStretch, 1.6f);
		ImGui::TableSetupColumn("Why it's here", ImGuiTableColumnFlags_WidthStretch, 2.6f);
		ImGui::TableHeadersRow();

		for (int i = 0; i < (int)r.findings.size(); i++) {
			const audit::Finding &f = r.findings[i];
			if (a.check_hide_info && f.level == audit::Level::Info) continue;
			if (a.check_kind_filter > 0 && (int)f.kind != a.check_kind_filter - 1) continue;
			shown++;

			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);
			ImGui::PushStyleColor(ImGuiCol_Text, Theme::V4(LevelColor(f.level)));
			ImGui::PushFont(Theme::F_UI, Theme::UI_PX * 0.78f);
			ImGui::AlignTextToFramePadding();
			ImGui::TextUnformatted(LevelName(f.level));
			ImGui::PopFont();
			ImGui::PopStyleColor();

			ImGui::TableSetColumnIndex(1);
			ImGui::PushID(i);
			// The whole row opens what it is about: a page in the editor, a
			// file in the Media tab.
			if (ImGui::Selectable(audit::KindName(f.kind), false, ImGuiSelectableFlags_SpanAllColumns)) {
				if (f.is_media) {
					a.tab = Tab::Media;
					for (int m = 0; m < (int)a.site.media.size(); m++)
						if (a.site.media[m].rel == f.where) a.media_selected = m;
				} else {
					a.RequestOpen(f.path);
				}
			}
			ImGui::SetItemTooltip("%s", audit::KindHelp(f.kind));
			if (ImGui::BeginPopupContextItem("row")) {
				if (ImGui::MenuItem(f.is_media ? "Show in Media" : "Open in the editor")) {
					if (f.is_media) a.tab = Tab::Media;
					else a.RequestOpen(f.path);
				}
				if (ImGui::MenuItem("Copy path")) ImGui::SetClipboardText(f.path.c_str());
				if (!f.subject.empty() && ImGui::MenuItem("Copy the target"))
					ImGui::SetClipboardText(f.subject.c_str());
				ImGui::EndPopup();
			}
			ImGui::PopID();

			ImGui::PushStyleColor(ImGuiCol_Text, Theme::V4(Theme::TEXT_DIM));
			ImGui::TableSetColumnIndex(2);
			ImGui::PushFont(Theme::F_MONO, Theme::UI_PX * 0.8f);
			if (f.line > 0) ImGui::Text("%s:%d", f.where.c_str(), f.line);
			else ImGui::TextUnformatted(f.where.c_str());
			ImGui::TableSetColumnIndex(3);
			ImGui::TextUnformatted(f.subject.empty() ? "—" : f.subject.c_str());
			ImGui::PopFont();
			ImGui::TableSetColumnIndex(4);
			ImGui::TextUnformatted(f.detail.c_str());
			ImGui::PopStyleColor();
		}
		ImGui::EndTable();
	}

	if (shown == 0) {
		ImGui::PushStyleColor(ImGuiCol_Text, Theme::V4(r.findings.empty() ? Theme::OK : Theme::TEXT_FAINT));
		ImGui::TextWrapped("%s", r.findings.empty()
		                             ? "Nothing to report: every link resolves, every image is there, and "
		                               "nothing in static/ is going unused."
		                             : "Nothing matches that filter.");
		ImGui::PopStyleColor();
	}
}
