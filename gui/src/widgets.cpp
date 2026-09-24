#include "widgets.h"

#include "theme.h"
#include "imgui_stdlib.h"

#include <algorithm>

namespace W {

void SectionHeader(const char *label) {
	ImGui::Dummy(ImVec2(0, 4));
	ImGui::PushFont(Theme::F_BOLD, Theme::UI_PX * 0.86f);
	ImGui::PushStyleColor(ImGuiCol_Text, Theme::V4(Theme::TEXT_DIM));
	ImGui::TextUnformatted(label);
	ImGui::PopStyleColor();
	ImGui::PopFont();
	ImVec2 p = ImGui::GetCursorScreenPos();
	ImGui::GetWindowDrawList()->AddLine(ImVec2(p.x, p.y + 1),
	                                    ImVec2(p.x + ImGui::GetContentRegionAvail().x, p.y + 1), Theme::BORDER);
	ImGui::Dummy(ImVec2(0, 5));
}

void Help(const char *text) {
	ImGui::SameLine();
	ImGui::PushStyleColor(ImGuiCol_Text, Theme::V4(Theme::TEXT_FAINT));
	ImGui::TextUnformatted("(?)");
	ImGui::PopStyleColor();
	if (ImGui::BeginItemTooltip()) {
		ImGui::PushTextWrapPos(ImGui::GetFontSize() * 24.0f);
		ImGui::TextUnformatted(text);
		ImGui::PopTextWrapPos();
		ImGui::EndTooltip();
	}
}

bool ToggleRow(const char *label, bool *v, const char *help) {
	bool changed = ImGui::Checkbox(label, v);
	if (help) {
		ImGui::PushStyleColor(ImGuiCol_Text, Theme::V4(Theme::TEXT_FAINT));
		ImGui::PushFont(Theme::F_UI, Theme::UI_PX * 0.85f);
		ImGui::Indent(26.0f);
		ImGui::TextWrapped("%s", help);
		ImGui::Unindent(26.0f);
		ImGui::PopFont();
		ImGui::PopStyleColor();
	}
	return changed;
}

void LabelRow(const char *label, float label_w) {
	ImGui::AlignTextToFramePadding();
	ImGui::PushStyleColor(ImGuiCol_Text, Theme::V4(Theme::TEXT_DIM));
	ImGui::TextUnformatted(label);
	ImGui::PopStyleColor();
	ImGui::SameLine(label_w);
	ImGui::SetNextItemWidth(-1);
}

bool Pill(const char *text, ImU32 bg, ImU32 fg, bool clickable) {
	ImGui::PushFont(Theme::F_UI, Theme::UI_PX * 0.8f);
	ImVec2 ts = ImGui::CalcTextSize(text);
	ImVec2 sz(ts.x + 14, ts.y + 6);
	ImVec2 p = ImGui::GetCursorScreenPos();
	bool pressed = false;
	if (clickable) {
		pressed = ImGui::InvisibleButton(text, sz);
		if (ImGui::IsItemHovered()) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
	} else {
		ImGui::Dummy(sz);
	}
	ImDrawList *dl = ImGui::GetWindowDrawList();
	dl->AddRectFilled(p, ImVec2(p.x + sz.x, p.y + sz.y), bg, sz.y * 0.5f);
	dl->AddText(ImVec2(p.x + 7, p.y + 3), fg, text);
	ImGui::PopFont();
	return pressed;
}

bool DangerButton(const char *label, const ImVec2 &size) {
	ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.42f, 0.18f, 0.18f, 1.0f));
	ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.56f, 0.22f, 0.22f, 1.0f));
	ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.66f, 0.26f, 0.26f, 1.0f));
	bool r = ImGui::Button(label, size);
	ImGui::PopStyleColor(3);
	return r;
}

bool PrimaryButton(const char *label, const ImVec2 &size) {
	ImGui::PushStyleColor(ImGuiCol_Button, Theme::V4(Theme::ACCENT_DIM));
	ImGui::PushStyleColor(ImGuiCol_ButtonHovered, Theme::V4(Theme::ACCENT));
	ImGui::PushStyleColor(ImGuiCol_ButtonActive, Theme::V4(Theme::ACCENT));
	bool r = ImGui::Button(label, size);
	ImGui::PopStyleColor(3);
	return r;
}

bool ToolButton(const char *label, const char *tip, bool active) {
	if (active) {
		ImGui::PushStyleColor(ImGuiCol_Button, Theme::V4(Theme::ACCENT_DIM));
		ImGui::PushStyleColor(ImGuiCol_Text, Theme::V4(Theme::TEXT));
	}
	bool r = ImGui::Button(label);
	if (active) ImGui::PopStyleColor(2);
	if (tip && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tip);
	return r;
}

void LogWell(const char *id, const std::string &text, const ImVec2 &size) {
	ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.04f, 0.05f, 0.06f, 1.0f));
	ImGui::PushFont(Theme::F_MONO, Theme::UI_PX * 0.85f);
	ImGui::BeginChild(id, size, ImGuiChildFlags_Borders, ImGuiWindowFlags_HorizontalScrollbar);
	ImGui::PushStyleColor(ImGuiCol_Text, Theme::V4(Theme::TEXT_DIM));
	ImGui::TextUnformatted(text.c_str());
	ImGui::PopStyleColor();
	// Stick to the bottom only while the reader is already there, so
	// scrolling back through a build log isn't yanked away by new output.
	if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 4.0f) ImGui::SetScrollHereY(1.0f);
	ImGui::EndChild();
	ImGui::PopFont();
	ImGui::PopStyleColor();
}

bool ChipEditor(const char *id, std::vector<std::string> *items, std::string *input,
                const std::vector<std::string> &suggestions) {
	bool changed = false;
	ImGui::PushID(id);
	float avail = ImGui::GetContentRegionAvail().x;
	float x0 = ImGui::GetCursorPosX();
	for (size_t i = 0; i < items->size(); i++) {
		ImGui::PushID((int)i);
		std::string label = (*items)[i] + "  x";
		ImGui::PushFont(Theme::F_UI, Theme::UI_PX * 0.85f);
		ImVec2 sz = ImGui::CalcTextSize(label.c_str());
		if (i && ImGui::GetCursorPosX() + sz.x + 22 < x0 + avail) ImGui::SameLine();
		ImGui::PushStyleColor(ImGuiCol_Button, Theme::V4(Theme::PANEL_HI));
		ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.42f, 0.18f, 0.18f, 1.0f));
		if (ImGui::Button(label.c_str())) {
			items->erase(items->begin() + (long)i);
			changed = true;
			ImGui::PopStyleColor(2);
			ImGui::PopFont();
			ImGui::PopID();
			break;
		}
		ImGui::SetItemTooltip("remove");
		ImGui::PopStyleColor(2);
		ImGui::PopFont();
		ImGui::PopID();
	}

	auto add = [&](const std::string &raw) {
		std::string t = raw;
		while (!t.empty() && (t.front() == ' ')) t.erase(t.begin());
		while (!t.empty() && (t.back() == ' ')) t.pop_back();
		if (t.empty()) return;
		if (std::find(items->begin(), items->end(), t) != items->end()) return;
		items->push_back(t);
		changed = true;
	};

	if (!items->empty() && ImGui::GetCursorPosX() + 250 < x0 + avail) ImGui::SameLine();
	ImGui::SetNextItemWidth(160);
	if (ImGui::InputTextWithHint("##add", "add a tag, Enter", input, ImGuiInputTextFlags_EnterReturnsTrue)) {
		add(*input);
		input->clear();
		ImGui::SetKeyboardFocusHere(-1);
	}
	if (!suggestions.empty()) {
		ImGui::SameLine();
		if (ImGui::BeginCombo("##suggest", "existing", ImGuiComboFlags_WidthFitPreview)) {
			for (const std::string &s : suggestions) {
				if (std::find(items->begin(), items->end(), s) != items->end()) continue;
				if (ImGui::Selectable(s.c_str())) add(s);
			}
			ImGui::EndCombo();
		}
	}
	ImGui::PopID();
	return changed;
}

} // namespace W
