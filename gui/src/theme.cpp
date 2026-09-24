#include "theme.h"

#include "util.h"

namespace Theme {

// The chrome: near-black panels with one blue accent. Everything else is a
// grey, so that the only saturated thing on screen is either the accent or
// the user's own content in the preview.
ImU32 BG = IM_COL32(0x14, 0x16, 0x1a, 0xff);
ImU32 PANEL = IM_COL32(0x1b, 0x1e, 0x24, 0xff);
ImU32 PANEL_HI = IM_COL32(0x23, 0x27, 0x2f, 0xff);
ImU32 RAIL = IM_COL32(0x10, 0x12, 0x16, 0xff);
ImU32 BORDER = IM_COL32(0x2c, 0x31, 0x3a, 0xff);
ImU32 ACCENT = IM_COL32(0x5a, 0xa9, 0xe6, 0xff);
ImU32 ACCENT_DIM = IM_COL32(0x2f, 0x5a, 0x7d, 0xff);
ImU32 TEXT = IM_COL32(0xe2, 0xe5, 0xea, 0xff);
ImU32 TEXT_DIM = IM_COL32(0x9a, 0xa2, 0xb0, 0xff);
ImU32 TEXT_FAINT = IM_COL32(0x66, 0x6e, 0x7c, 0xff);
ImU32 WARN = IM_COL32(0xe0, 0xa8, 0x4e, 0xff);
ImU32 DANGER = IM_COL32(0xe0, 0x6c, 0x6c, 0xff);
ImU32 OK = IM_COL32(0x6c, 0xc4, 0x8a, 0xff);
ImU32 DRAFT = IM_COL32(0xc9, 0x8a, 0xe0, 0xff);

ImFont *F_UI = nullptr, *F_BOLD = nullptr, *F_ITALIC = nullptr, *F_MONO = nullptr;
float UI_PX = 17.0f;

ImVec4 V4(ImU32 c) { return ImGui::ColorConvertU32ToFloat4(c); }

ScopedFont::ScopedFont(ImFont *f, float px) {
	ImGui::PushFont(f, px);
	pushed = true;
}
ScopedFont::~ScopedFont() {
	if (pushed) ImGui::PopFont();
}

void LoadFonts(ImGuiIO &io, float base_px) {
	UI_PX = base_px;
	struct Face { ImFont **slot; const char *files[6]; };
	// Ordered by preference; the first path that exists wins. Noto Sans is
	// what this box has; DejaVu and Liberation cover most other distros.
	const Face faces[] = {
	    {&F_UI, {"/usr/share/fonts/google-noto/NotoSans-Regular.ttf",
	             "/usr/share/fonts/dejavu-sans-fonts/DejaVuSans.ttf",
	             "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
	             "/usr/share/fonts/liberation-sans/LiberationSans-Regular.ttf", nullptr, nullptr}},
	    {&F_BOLD, {"/usr/share/fonts/google-noto/NotoSans-Bold.ttf",
	               "/usr/share/fonts/dejavu-sans-fonts/DejaVuSans-Bold.ttf",
	               "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf",
	               "/usr/share/fonts/liberation-sans/LiberationSans-Bold.ttf", nullptr, nullptr}},
	    {&F_ITALIC, {"/usr/share/fonts/google-noto/NotoSans-Italic.ttf",
	                 "/usr/share/fonts/dejavu-sans-fonts/DejaVuSans-Oblique.ttf",
	                 "/usr/share/fonts/truetype/dejavu/DejaVuSans-Oblique.ttf",
	                 "/usr/share/fonts/liberation-sans/LiberationSans-Italic.ttf", nullptr, nullptr}},
	    {&F_MONO, {"/usr/share/fonts/google-noto/NotoSansMono-Regular.ttf",
	               "/usr/share/fonts/dejavu-sans-mono-fonts/DejaVuSansMono.ttf",
	               "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf",
	               "/usr/share/fonts/liberation-mono/LiberationMono-Regular.ttf", nullptr, nullptr}},
	};
	for (const Face &f : faces) {
		for (const char *p : f.files) {
			if (!p) break;
			if (!util::Exists(p)) continue;
			*f.slot = io.Fonts->AddFontFromFileTTF(p, base_px);
			break;
		}
	}
	if (F_UI) io.FontDefault = F_UI;
}

void Apply() {
	ImGuiStyle &s = ImGui::GetStyle();
	s.WindowRounding = 0.0f;
	s.ChildRounding = 6.0f;
	s.FrameRounding = 5.0f;
	s.PopupRounding = 6.0f;
	s.GrabRounding = 5.0f;
	s.TabRounding = 5.0f;
	s.ScrollbarRounding = 8.0f;
	s.WindowPadding = ImVec2(14, 12);
	s.FramePadding = ImVec2(9, 6);
	s.ItemSpacing = ImVec2(9, 7);
	s.ItemInnerSpacing = ImVec2(7, 5);
	s.CellPadding = ImVec2(8, 6);
	s.ScrollbarSize = 12.0f;
	s.WindowBorderSize = 0.0f;
	s.ChildBorderSize = 1.0f;
	s.FrameBorderSize = 1.0f;
	s.SeparatorTextBorderSize = 1.0f;

	ImVec4 *c = s.Colors;
	c[ImGuiCol_Text] = V4(TEXT);
	c[ImGuiCol_TextDisabled] = V4(TEXT_FAINT);
	c[ImGuiCol_WindowBg] = V4(BG);
	c[ImGuiCol_ChildBg] = V4(PANEL);
	c[ImGuiCol_PopupBg] = V4(PANEL);
	c[ImGuiCol_Border] = V4(BORDER);
	c[ImGuiCol_BorderShadow] = ImVec4(0, 0, 0, 0);
	c[ImGuiCol_FrameBg] = V4(IM_COL32(0x14, 0x17, 0x1c, 0xff));
	c[ImGuiCol_FrameBgHovered] = V4(IM_COL32(0x1e, 0x22, 0x2a, 0xff));
	c[ImGuiCol_FrameBgActive] = V4(IM_COL32(0x22, 0x27, 0x30, 0xff));
	c[ImGuiCol_TitleBg] = V4(RAIL);
	c[ImGuiCol_TitleBgActive] = V4(RAIL);
	c[ImGuiCol_MenuBarBg] = V4(RAIL);
	c[ImGuiCol_ScrollbarBg] = ImVec4(0, 0, 0, 0);
	c[ImGuiCol_ScrollbarGrab] = V4(IM_COL32(0x33, 0x39, 0x44, 0xff));
	c[ImGuiCol_ScrollbarGrabHovered] = V4(IM_COL32(0x44, 0x4c, 0x59, 0xff));
	c[ImGuiCol_ScrollbarGrabActive] = V4(ACCENT_DIM);
	c[ImGuiCol_CheckMark] = V4(ACCENT);
	c[ImGuiCol_SliderGrab] = V4(ACCENT_DIM);
	c[ImGuiCol_SliderGrabActive] = V4(ACCENT);
	c[ImGuiCol_Button] = V4(IM_COL32(0x25, 0x2a, 0x33, 0xff));
	c[ImGuiCol_ButtonHovered] = V4(IM_COL32(0x31, 0x38, 0x44, 0xff));
	c[ImGuiCol_ButtonActive] = V4(ACCENT_DIM);
	c[ImGuiCol_Header] = V4(IM_COL32(0x24, 0x2a, 0x34, 0xff));
	c[ImGuiCol_HeaderHovered] = V4(IM_COL32(0x2d, 0x34, 0x40, 0xff));
	c[ImGuiCol_HeaderActive] = V4(ACCENT_DIM);
	c[ImGuiCol_Separator] = V4(BORDER);
	c[ImGuiCol_SeparatorHovered] = V4(ACCENT_DIM);
	c[ImGuiCol_SeparatorActive] = V4(ACCENT);
	c[ImGuiCol_ResizeGrip] = V4(IM_COL32(0x2c, 0x31, 0x3a, 0xff));
	c[ImGuiCol_ResizeGripHovered] = V4(ACCENT_DIM);
	c[ImGuiCol_ResizeGripActive] = V4(ACCENT);
	c[ImGuiCol_Tab] = V4(IM_COL32(0x1a, 0x1e, 0x25, 0xff));
	c[ImGuiCol_TabHovered] = V4(IM_COL32(0x2b, 0x32, 0x3d, 0xff));
	c[ImGuiCol_TabSelected] = V4(PANEL_HI);
	c[ImGuiCol_TabSelectedOverline] = V4(ACCENT);
	c[ImGuiCol_TableHeaderBg] = V4(IM_COL32(0x1e, 0x22, 0x2a, 0xff));
	c[ImGuiCol_TableBorderStrong] = V4(BORDER);
	c[ImGuiCol_TableBorderLight] = V4(IM_COL32(0x25, 0x29, 0x31, 0xff));
	c[ImGuiCol_TableRowBg] = ImVec4(0, 0, 0, 0);
	c[ImGuiCol_TableRowBgAlt] = V4(IM_COL32(0xff, 0xff, 0xff, 0x06));
	c[ImGuiCol_TextSelectedBg] = V4(IM_COL32(0x2f, 0x5a, 0x7d, 0xcc));
	c[ImGuiCol_NavCursor] = V4(ACCENT);
	c[ImGuiCol_ModalWindowDimBg] = ImVec4(0, 0, 0, 0.55f);
}

} // namespace Theme

namespace Paper {

ImU32 THEME = IM_COL32(29, 30, 32, 255);
ImU32 ENTRY = IM_COL32(46, 46, 51, 255);
ImU32 PRIMARY = IM_COL32(218, 218, 219, 255);
ImU32 SECONDARY = IM_COL32(155, 156, 157, 255);
ImU32 TERTIARY = IM_COL32(65, 66, 68, 255);
ImU32 CONTENT = IM_COL32(196, 196, 197, 255);
ImU32 CODE_BG = IM_COL32(55, 56, 62, 255);
ImU32 CODE_BLOCK_BG = IM_COL32(46, 46, 51, 255);
ImU32 BORDER = IM_COL32(51, 51, 51, 255);

ImU32 L_THEME = IM_COL32(255, 255, 255, 255);
ImU32 L_ENTRY = IM_COL32(255, 255, 255, 255);
ImU32 L_PRIMARY = IM_COL32(30, 30, 30, 255);
ImU32 L_SECONDARY = IM_COL32(108, 108, 108, 255);
ImU32 L_TERTIARY = IM_COL32(214, 214, 214, 255);
ImU32 L_CONTENT = IM_COL32(31, 31, 31, 255);
ImU32 L_CODE_BG = IM_COL32(245, 245, 245, 255);
ImU32 L_CODE_BLOCK_BG = IM_COL32(28, 29, 33, 255);
ImU32 L_BORDER = IM_COL32(238, 238, 238, 255);

Palette Get(bool dark) {
	Palette p;
	if (dark) {
		p = {THEME, ENTRY, PRIMARY, SECONDARY, TERTIARY, CONTENT, CODE_BG, CODE_BLOCK_BG, BORDER,
		     IM_COL32(0x7f, 0xb3, 0xe8, 0xff)};
	} else {
		p = {L_THEME, L_ENTRY, L_PRIMARY, L_SECONDARY, L_TERTIARY, L_CONTENT, L_CODE_BG, L_CODE_BLOCK_BG, L_BORDER,
		     IM_COL32(0x1a, 0x5c, 0xa8, 0xff)};
	}
	return p;
}

} // namespace Paper
