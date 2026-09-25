// Content tab -- every page under content/, and the file operations on them.
#include "app.h"
#include "util.h"
#include "widgets.h"

#include "imgui.h"
#include "imgui_stdlib.h"

#include <algorithm>

namespace {

bool SetDraft(const std::string &path, bool draft, std::string *err) {
	std::string text;
	if (!util::ReadFile(path, &text)) { *err = "could not read " + path; return false; }
	fm::Doc d = fm::Parse(text);
	if (d.kind == fm::Kind::None) {
		// No front matter at all: give it some rather than silently doing
		// nothing, otherwise the toggle looks broken.
		d.kind = fm::Kind::TOML;
		d.SetStr("title", util::BaseName(util::StripExt(path)));
	}
	d.SetBool("draft", draft);
	if (!util::WriteFileAtomic(path, fm::Serialize(d))) { *err = "could not write " + path; return false; }
	return true;
}

bool Duplicate(const site::Post &p, std::string *out_path, std::string *err) {
	std::string text;
	if (!util::ReadFile(p.path, &text)) { *err = "could not read " + p.path; return false; }
	fm::Doc d = fm::Parse(text);
	std::string title = d.GetStr("title", util::BaseName(util::StripExt(p.path)));
	d.SetStr("title", title + " (copy)");
	d.SetBool("draft", true);
	d.SetStr("date", util::NowRFC3339());
	std::string base = util::StripExt(p.path);
	std::string dest = base + "-copy.md";
	for (int i = 2; util::Exists(dest); i++) dest = base + "-copy-" + std::to_string(i) + ".md";
	if (!util::WriteFileAtomic(dest, fm::Serialize(d))) { *err = "could not write " + dest; return false; }
	*out_path = dest;
	return true;
}

void DetailPanel(App &a, int idx) {
	ImGui::BeginChild("detail", ImVec2(330, 0), ImGuiChildFlags_Borders);
	if (idx < 0 || idx >= (int)a.site.posts.size()) {
		ImGui::PushStyleColor(ImGuiCol_Text, Theme::V4(Theme::TEXT_FAINT));
		ImGui::TextWrapped("Select a page to see its details, or press Ctrl+N to write a new one.");
		ImGui::PopStyleColor();
		ImGui::EndChild();
		return;
	}
	const site::Post &p = a.site.posts[idx];

	ImGui::PushFont(Theme::F_BOLD, Theme::UI_PX * 1.12f);
	ImGui::TextWrapped("%s", p.title.c_str());
	ImGui::PopFont();

	ImGui::Dummy(ImVec2(0, 2));
	if (p.draft) {
		W::Pill("draft", IM_COL32(0x3a, 0x2a, 0x45, 0xff), Theme::DRAFT);
		ImGui::SameLine();
	}
	if (!p.section.empty()) {
		W::Pill(p.section.c_str(), Theme::PANEL_HI, Theme::TEXT_DIM);
		ImGui::SameLine();
	}
	ImGui::NewLine();

	W::SectionHeader("Where it lives");
	ImGui::PushStyleColor(ImGuiCol_Text, Theme::V4(Theme::TEXT_DIM));
	ImGui::PushFont(Theme::F_MONO, Theme::UI_PX * 0.82f);
	ImGui::TextWrapped("content/%s", p.rel.c_str());
	ImGui::TextWrapped("%s", p.permalink.c_str());
	ImGui::PopFont();
	ImGui::PopStyleColor();

	W::SectionHeader("Summary");
	ImGui::PushStyleColor(ImGuiCol_Text, Theme::V4(p.summary.empty() ? Theme::TEXT_FAINT : Theme::TEXT_DIM));
	ImGui::TextWrapped("%s", p.summary.empty() ? "(none set -- Hugo will use the opening words)" : p.summary.c_str());
	ImGui::PopStyleColor();

	if (!p.tags.empty()) {
		W::SectionHeader("Tags");
		for (const std::string &t : p.tags) {
			W::Pill(t.c_str(), Theme::PANEL_HI, Theme::TEXT_DIM);
			ImGui::SameLine();
		}
		ImGui::NewLine();
	}

	W::SectionHeader("Facts");
	ImGui::PushStyleColor(ImGuiCol_Text, Theme::V4(Theme::TEXT_DIM));
	ImGui::Text("%d words  ·  about %d min read", p.words, p.words / 200 + 1);
	ImGui::Text("date  %s", p.date.empty() ? "(none)" : util::PrettyDate(p.date).c_str());
	ImGui::Text("file  %s", util::HumanSize(p.size).c_str());
	ImGui::Text("saved %s", util::PrettyEpoch(p.mtime).c_str());
	ImGui::PopStyleColor();

	ImGui::Dummy(ImVec2(0, 8));
	W::SectionHeader("Actions");
	if (W::PrimaryButton("Edit", ImVec2(-1, 0))) a.RequestOpen(p.path);

	std::string path = p.path;
	bool draft = p.draft;
	if (ImGui::Button(draft ? "Publish (clear draft)" : "Mark as draft", ImVec2(-1, 0))) {
		std::string err;
		if (SetDraft(path, !draft, &err)) {
			a.ReloadAll();
			a.Notify(draft ? "published" : "marked as draft");
		} else a.Notify(err, true);
	}
	ImGui::SetItemTooltip("draft = true keeps it out of a normal `hugo` build");

	ImGui::BeginDisabled(!a.server.Running());
	if (ImGui::Button("Open in browser", ImVec2(-1, 0))) util::OpenURL(a.LocalURL(p));
	ImGui::EndDisabled();
	if (!a.server.Running()) ImGui::SetItemTooltip("start the preview server first (top right)");

	if (ImGui::Button("Duplicate", ImVec2(-1, 0))) {
		std::string dest, err;
		if (Duplicate(p, &dest, &err)) {
			a.ReloadAll();
			a.RequestOpen(dest);
			a.Notify("duplicated to " + util::BaseName(dest));
		} else a.Notify(err, true);
	}

	if (ImGui::Button("Rename...", ImVec2(-1, 0))) ImGui::OpenPopup("rename");
	if (ImGui::BeginPopup("rename")) {
		static std::string new_slug;
		if (ImGui::IsWindowAppearing()) new_slug = util::BaseName(util::StripExt(path));
		ImGui::TextUnformatted("New file name (without .md)");
		ImGui::SetNextItemWidth(260);
		bool go = ImGui::InputText("##slug", &new_slug, ImGuiInputTextFlags_EnterReturnsTrue);
		ImGui::SameLine();
		go |= ImGui::Button("Rename");
		if (go && !new_slug.empty()) {
			std::string dest = util::DirName(path) + "/" + util::Slugify(new_slug) + ".md";
			std::string err;
			if (site::RenamePost(path, dest, &err)) {
				if (a.ed.path == path) a.ed.path = dest;
				a.ReloadAll();
				a.Notify("renamed to " + util::BaseName(dest));
			} else a.Notify(err, true);
			ImGui::CloseCurrentPopup();
		}
		ImGui::PushStyleColor(ImGuiCol_Text, Theme::V4(Theme::TEXT_FAINT));
		ImGui::TextWrapped("The file name is the URL: /%s%s/", p.section.empty() ? "" : (p.section + "/").c_str(),
		                   util::Slugify(new_slug).c_str());
		ImGui::PopStyleColor();
		ImGui::EndPopup();
	}

	ImGui::Dummy(ImVec2(0, 4));
	if (W::DangerButton("Delete...", ImVec2(-1, 0))) a.confirm_delete = path;
	ImGui::EndChild();
}

} // namespace

void DrawContentTab(App &a) {
	// ---- toolbar
	if (W::PrimaryButton("+  New post")) a.want_new_post = true;
	ImGui::SetItemTooltip("Ctrl+N");
	ImGui::SameLine();
	if (ImGui::Button("Rescan")) {
		a.ReloadAll();
		a.Notify("rescanned the site");
	}
	ImGui::SetItemTooltip("re-read content/ from disk (Ctrl+R)");

	ImGui::SameLine(0, 24);
	ImGui::SetNextItemWidth(260);
	ImGui::InputTextWithHint("##filter", "filter by title, tag or path", &a.filter);
	ImGui::SameLine();

	std::vector<std::string> sections = {"all sections"};
	for (const auto &s : a.site.sections) sections.push_back(s);
	sections.push_back("(root pages)");
	if (a.section_filter >= (int)sections.size()) a.section_filter = 0;
	ImGui::SetNextItemWidth(160);
	if (ImGui::BeginCombo("##section", sections[a.section_filter].c_str())) {
		for (int i = 0; i < (int)sections.size(); i++)
			if (ImGui::Selectable(sections[i].c_str(), a.section_filter == i)) a.section_filter = i;
		ImGui::EndCombo();
	}
	ImGui::SameLine();
	ImGui::Checkbox("drafts", &a.show_drafts);
	ImGui::SetItemTooltip("show pages with draft = true");

	// ---- which rows survive the filter
	std::vector<int> rows;
	for (int i = 0; i < (int)a.site.posts.size(); i++) {
		const site::Post &p = a.site.posts[i];
		if (!a.show_drafts && p.draft) continue;
		if (a.section_filter > 0) {
			const std::string &want = sections[a.section_filter];
			if (want == "(root pages)") {
				if (!p.section.empty()) continue;
			} else if (p.section != want) continue;
		}
		if (!a.filter.empty()) {
			bool hit = util::IContains(p.title, a.filter) || util::IContains(p.rel, a.filter) ||
			           util::IContains(p.summary, a.filter);
			for (const auto &t : p.tags) hit = hit || util::IContains(t, a.filter);
			if (!hit) continue;
		}
		rows.push_back(i);
	}

	ImGui::Dummy(ImVec2(0, 4));
	float detail_w = 330 + 14;
	ImGui::BeginChild("list", ImVec2(ImGui::GetContentRegionAvail().x - detail_w, 0), ImGuiChildFlags_None);
	ImGuiTableFlags tf = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_Sortable |
	                     ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingStretchProp;
	if (ImGui::BeginTable("posts", 6, tf)) {
		ImGui::TableSetupScrollFreeze(0, 1);
		ImGui::TableSetupColumn("Title", ImGuiTableColumnFlags_WidthStretch, 3.0f);
		ImGui::TableSetupColumn("Section", ImGuiTableColumnFlags_WidthStretch, 1.0f);
		ImGui::TableSetupColumn("Date", ImGuiTableColumnFlags_WidthStretch, 1.3f);
		ImGui::TableSetupColumn("State", ImGuiTableColumnFlags_WidthStretch, 0.9f);
		ImGui::TableSetupColumn("Words", ImGuiTableColumnFlags_WidthStretch, 0.7f);
		ImGui::TableSetupColumn("Tags", ImGuiTableColumnFlags_WidthStretch, 1.6f);
		ImGui::TableHeadersRow();

		if (ImGuiTableSortSpecs *ss = ImGui::TableGetSortSpecs()) {
			if (ss->SpecsCount > 0) {
				const ImGuiTableColumnSortSpecs &s = ss->Specs[0];
				const std::vector<site::Post> &posts = a.site.posts;
				std::sort(rows.begin(), rows.end(), [&](int x, int y) {
					const site::Post &A = posts[x], &B = posts[y];
					int c = 0;
					switch (s.ColumnIndex) {
					case 0: c = util::Lower(A.title).compare(util::Lower(B.title)); break;
					case 1: c = A.section.compare(B.section); break;
					case 2: c = A.date.compare(B.date); break;
					case 3: c = (int)A.draft - (int)B.draft; break;
					case 4: c = A.words - B.words; break;
					default: c = util::Join(A.tags, ",").compare(util::Join(B.tags, ",")); break;
					}
					if (c == 0) c = A.rel.compare(B.rel);
					return s.SortDirection == ImGuiSortDirection_Ascending ? c < 0 : c > 0;
				});
			}
		}

		for (int i : rows) {
			const site::Post &p = a.site.posts[i];
			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);
			ImGui::PushID(i);
			bool sel = (a.selected == i);
			if (ImGui::Selectable(p.title.c_str(), sel, ImGuiSelectableFlags_SpanAllColumns)) a.selected = i;
			if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) a.RequestOpen(p.path);
			if (ImGui::BeginPopupContextItem("row")) {
				a.selected = i;
				if (ImGui::MenuItem("Edit")) a.RequestOpen(p.path);
				if (ImGui::MenuItem("Open in browser", nullptr, false, a.server.Running()))
					util::OpenURL(a.LocalURL(p));
				if (ImGui::MenuItem("Copy path")) ImGui::SetClipboardText(p.path.c_str());
				ImGui::Separator();
				if (ImGui::MenuItem("Delete...")) a.confirm_delete = p.path;
				ImGui::EndPopup();
			}
			ImGui::PopID();

			ImGui::TableSetColumnIndex(1);
			ImGui::PushStyleColor(ImGuiCol_Text, Theme::V4(Theme::TEXT_DIM));
			ImGui::TextUnformatted(p.section.empty() ? "—" : p.section.c_str());
			ImGui::TableSetColumnIndex(2);
			ImGui::TextUnformatted(p.date.empty() ? "—" : util::PrettyDate(p.date).c_str());
			ImGui::PopStyleColor();

			ImGui::TableSetColumnIndex(3);
			if (p.draft) {
				ImGui::PushStyleColor(ImGuiCol_Text, Theme::V4(Theme::DRAFT));
				ImGui::TextUnformatted("draft");
				ImGui::PopStyleColor();
			} else if (p.is_branch) {
				ImGui::PushStyleColor(ImGuiCol_Text, Theme::V4(Theme::TEXT_FAINT));
				ImGui::TextUnformatted("section");
				ImGui::PopStyleColor();
			} else {
				ImGui::PushStyleColor(ImGuiCol_Text, Theme::V4(Theme::OK));
				ImGui::TextUnformatted("live");
				ImGui::PopStyleColor();
			}

			ImGui::TableSetColumnIndex(4);
			ImGui::PushStyleColor(ImGuiCol_Text, Theme::V4(Theme::TEXT_DIM));
			ImGui::Text("%d", p.words);
			ImGui::TableSetColumnIndex(5);
			ImGui::TextUnformatted(util::Join(p.tags, ", ").c_str());
			ImGui::PopStyleColor();
		}
		ImGui::EndTable();
	}
	if (rows.empty()) {
		ImGui::PushStyleColor(ImGuiCol_Text, Theme::V4(Theme::TEXT_FAINT));
		ImGui::TextWrapped("Nothing matches. Clear the filter, or press New post.");
		ImGui::PopStyleColor();
	}
	ImGui::EndChild();

	ImGui::SameLine();
	DetailPanel(a, a.selected);
}

// --------------------------------------------------------------- New post

void DrawNewPostModal(App &a) {
	static std::string title, slug, section;
	static std::vector<std::string> tags;
	static std::string tag_input;
	static bool draft = true;
	static bool slug_edited = false;

	ImGui::SetNextWindowSize(ImVec2(560, 0), ImGuiCond_Always);
	if (!ImGui::BeginPopupModal("New post", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return;

	if (ImGui::IsWindowAppearing()) {
		title.clear();
		slug.clear();
		tags.clear();
		tag_input.clear();
		draft = true;
		slug_edited = false;
		section = a.site.sections.empty() ? "posts" : a.site.sections[0];
		ImGui::SetKeyboardFocusHere();
	}

	W::LabelRow("Title", 110);
	if (ImGui::InputText("##title", &title) && !slug_edited) slug = util::Slugify(title);

	W::LabelRow("Section", 110);
	if (ImGui::BeginCombo("##section", section.c_str())) {
		for (const std::string &s : a.site.sections)
			if (ImGui::Selectable(s.c_str(), s == section)) section = s;
		if (ImGui::Selectable("(root of content/)", section.empty())) section = "";
		ImGui::EndCombo();
	}
	ImGui::SameLine();
	ImGui::SetNextItemWidth(150);
	ImGui::InputTextWithHint("##newsection", "or a new one", &section);

	W::LabelRow("File name", 110);
	if (ImGui::InputText("##slug", &slug)) slug_edited = true;

	std::string clean = util::Slugify(slug);
	std::string rel = (section.empty() ? "" : section + "/") + clean + ".md";
	ImGui::PushStyleColor(ImGuiCol_Text, Theme::V4(Theme::TEXT_FAINT));
	ImGui::Text("      content/%s   ->   %s", rel.c_str(), site::Permalink(rel, "").c_str());
	ImGui::PopStyleColor();

	W::LabelRow("Tags", 110);
	ImGui::NewLine();
	ImGui::Indent(110);
	W::ChipEditor("newtags", &tags, &tag_input, a.AllTags());
	ImGui::Unindent(110);

	ImGui::Dummy(ImVec2(0, 4));
	ImGui::Indent(110);
	W::ToggleRow("Start as a draft", &draft,
	             "Drafts are visible in the local preview but left out of a normal build.");
	ImGui::Unindent(110);

	std::string exists_path = a.site.root + "/content/" + rel;
	bool clash = !clean.empty() && util::Exists(exists_path);
	if (clash) {
		ImGui::PushStyleColor(ImGuiCol_Text, Theme::V4(Theme::DANGER));
		ImGui::TextWrapped("content/%s already exists.", rel.c_str());
		ImGui::PopStyleColor();
	}

	ImGui::Dummy(ImVec2(0, 8));
	ImGui::BeginDisabled(clean.empty() || clash);
	if (W::PrimaryButton("Create and edit", ImVec2(160, 0))) {
		std::string path, err;
		if (site::NewPost(a.site, section, title, clean, &path, &err)) {
			// The archetype is the source of truth for the front matter; the
			// dialog's own fields are then applied over the top of it.
			std::string text;
			util::ReadFile(path, &text);
			fm::Doc d = fm::Parse(text);
			if (!title.empty()) d.SetStr("title", title);
			d.SetBool("draft", draft);
			if (!tags.empty()) d.SetList("tags", tags);
			if (!d.Has("date")) d.SetStr("date", util::NowRFC3339());
			util::WriteFileAtomic(path, fm::Serialize(d));
			a.ReloadAll();
			a.RequestOpen(path);
			a.Notify("created content/" + rel);
			ImGui::CloseCurrentPopup();
		} else {
			a.Notify(err, true);
		}
	}
	ImGui::EndDisabled();
	ImGui::SameLine();
	if (ImGui::Button("Cancel", ImVec2(110, 0))) ImGui::CloseCurrentPopup();
	ImGui::EndPopup();
}
