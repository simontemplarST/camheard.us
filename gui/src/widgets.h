// widgets -- the handful of shared UI pieces the tabs share.
#pragma once

#include "imgui.h"

#include <string>
#include <vector>

namespace W {

// A left-aligned section title with a hairline under it.
void SectionHeader(const char *label);
// A (?) that shows `text` on hover.
void Help(const char *text);
// Checkbox + label + one line of explanation underneath, greyed.
bool ToggleRow(const char *label, bool *v, const char *help = nullptr);
// A labelled row: fixed-width caption on the left, the widget on the right.
void LabelRow(const char *label, float label_w = 150.0f);
// A small coloured badge (draft, section name, a tag).
bool Pill(const char *text, ImU32 bg, ImU32 fg, bool clickable = false);
// A button that reads as destructive.
bool DangerButton(const char *label, const ImVec2 &size = ImVec2(0, 0));
// A button drawn in the accent colour, for the one primary action on screen.
bool PrimaryButton(const char *label, const ImVec2 &size = ImVec2(0, 0));
// Toolbar button: compact, square-ish, with a tooltip.
bool ToolButton(const char *label, const char *tip, bool active = false);
// A read-only monospace log well that sticks to the bottom while it grows.
void LogWell(const char *id, const std::string &text, const ImVec2 &size);
// Editable list of short strings shown as removable chips, plus an "add"
// field. Returns true when the list changed.
bool ChipEditor(const char *id, std::vector<std::string> *items, std::string *input,
                const std::vector<std::string> &suggestions);

} // namespace W
