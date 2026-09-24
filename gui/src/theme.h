// theme -- the app's palette, the fonts, and the ImGui style.
//
// Two palettes live here on purpose. `Theme::` is the chrome of the editor
// itself (dark, quiet, so it doesn't compete with the content). `Paper::` is
// PaperMod's own dark-mode CSS custom properties, lifted verbatim from
// themes/PaperMod/assets/css/core/theme-vars.css -- the preview pane paints
// with those so what you see matches what the built site will look like.
#pragma once

#include "imgui.h"

#include <string>

namespace Theme {

extern ImU32 BG, PANEL, PANEL_HI, RAIL, BORDER, ACCENT, ACCENT_DIM;
extern ImU32 TEXT, TEXT_DIM, TEXT_FAINT, WARN, DANGER, OK, DRAFT;

// One face each; ImGui 1.92+ rasterises a font at whatever size it is pushed
// at, so headings, body and captions all come out of these four rather than
// one baked ImFont per size.
extern ImFont *F_UI, *F_BOLD, *F_ITALIC, *F_MONO;
extern float UI_PX;

// Probes a short list of system font paths (Noto Sans, then DejaVu, then
// Liberation). Every one of these may be missing -- on a bare container none
// are -- in which case the fields stay null and ImGui's built-in font is
// used. PushFont(nullptr, size) is a documented no-op, so callers never have
// to check.
void LoadFonts(ImGuiIO &io, float base_px);
void Apply();  // colors + spacing + rounding into ImGui's style

// Convenience: push/pop the bold or mono face at a size.
struct ScopedFont {
	bool pushed = false;
	ScopedFont(ImFont *f, float px);
	~ScopedFont();
};

ImVec4 V4(ImU32 c);

} // namespace Theme

namespace Paper {

// PaperMod dark mode (:root[data-theme="dark"]).
extern ImU32 THEME, ENTRY, PRIMARY, SECONDARY, TERTIARY, CONTENT, CODE_BG, CODE_BLOCK_BG, BORDER;
// PaperMod light mode (:root) -- the preview can be flipped, same as the
// site's own theme toggle.
extern ImU32 L_THEME, L_ENTRY, L_PRIMARY, L_SECONDARY, L_TERTIARY, L_CONTENT, L_CODE_BG, L_CODE_BLOCK_BG, L_BORDER;

struct Palette {
	ImU32 theme, entry, primary, secondary, tertiary, content, code_bg, code_block_bg, border, link;
};
Palette Get(bool dark);

} // namespace Paper
