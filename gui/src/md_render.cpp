#include "md_render.h"

#include "util.h"

#include <cmath>

namespace md {
namespace {

ImFont *FaceFor(const Span &s) {
	if (s.code) return Theme::F_MONO;
	if (s.bold) return Theme::F_BOLD;
	if (s.italic) return Theme::F_ITALIC;
	return Theme::F_UI;
}

// Splits into runs of "word + the whitespace that follows it", which is the
// unit the wrapper moves around. Keeping the trailing space attached is what
// stops a wrapped line from starting with a stray space.
std::vector<std::string> Tokenize(const std::string &s) {
	std::vector<std::string> out;
	size_t i = 0;
	while (i < s.size()) {
		size_t j = i;
		while (j < s.size() && !std::isspace((unsigned char)s[j])) j++;
		while (j < s.size() && std::isspace((unsigned char)s[j])) j++;
		out.push_back(s.substr(i, j - i));
		i = j;
	}
	return out;
}

std::string RTrim(const std::string &s) {
	size_t e = s.size();
	while (e > 0 && std::isspace((unsigned char)s[e - 1])) e--;
	return s.substr(0, e);
}

} // namespace

void RenderSpans(const std::vector<Span> &spans, const RenderStyle &st, float size_px, ImU32 color) {
	const float avail = ImGui::GetContentRegionAvail().x;
	const float x_left = ImGui::GetCursorPosX();
	float x_max = x_left + (avail > 40.0f ? avail : 40.0f);
	// A caller drawing into a narrower column than the window says where it
	// ends, so the manual wrap and any pushed text wrap position agree.
	if (st.wrap_x > x_left + 40.0f && st.wrap_x < x_max) x_max = st.wrap_x;
	bool line_has_content = false;
	ImDrawList *dl = ImGui::GetWindowDrawList();

	for (const Span &sp : spans) {
		if (sp.image) {
			// No image decoder is vendored here, so an image is drawn as a
			// labelled plate. It still earns its place: the plate is red when
			// the file isn't on disk, which catches the single most common
			// mistake in a post -- a src that doesn't resolve.
			bool ok = st.image_exists ? st.image_exists(sp.url) : true;
			if (line_has_content) { ImGui::NewLine(); line_has_content = false; }
			ImVec2 p = ImGui::GetCursorScreenPos();
			float w = ImGui::GetContentRegionAvail().x;
			float h = size_px * 3.2f;
			ImU32 edge = ok ? st.pal.tertiary : IM_COL32(0xc0, 0x50, 0x50, 0xff);
			dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), st.pal.entry, 6.0f);
			dl->AddRect(p, ImVec2(p.x + w, p.y + h), edge, 6.0f, 0, 1.5f);
			std::string label = (ok ? "image  " : "image not found  ") + sp.url;
			std::string alt = sp.text.empty() ? std::string("(no alt text)") : sp.text;
			ImGui::PushFont(Theme::F_UI, size_px * 0.85f);
			ImVec2 ls = ImGui::CalcTextSize(label.c_str());
			dl->AddText(ImVec2(p.x + w * 0.5f - ls.x * 0.5f, p.y + h * 0.5f - ls.y - 2), edge, label.c_str());
			ImVec2 as = ImGui::CalcTextSize(alt.c_str());
			dl->AddText(ImVec2(p.x + w * 0.5f - as.x * 0.5f, p.y + h * 0.5f + 2), st.pal.secondary, alt.c_str());
			ImGui::PopFont();
			ImGui::Dummy(ImVec2(w, h));
			continue;
		}

		ImFont *face = FaceFor(sp);
		float px = sp.code ? size_px * 0.92f : size_px;
		ImU32 col = sp.url.empty() ? color : st.pal.link;

		for (const std::string &tok : Tokenize(sp.text)) {
			std::string draw = RTrim(tok);
			ImGui::PushFont(face, px);
			ImVec2 full = ImGui::CalcTextSize(tok.c_str());
			ImVec2 word = ImGui::CalcTextSize(draw.c_str());
			bool wrapped = false;
			if (line_has_content && ImGui::GetCursorPosX() + word.x > x_max) {
				ImGui::NewLine();
				line_has_content = false;
				wrapped = true;
			}
			// The space that ended the previous token is already spent when we
			// wrap, so a wrapped line starts flush.
			const char *text = wrapped ? draw.c_str() : tok.c_str();
			if (*text == '\0') { ImGui::PopFont(); continue; }

			ImGui::PushStyleColor(ImGuiCol_Text, Theme::V4(col));
			if (sp.code) {
				// Inline code gets PaperMod's pill: a rounded plate behind the
				// text, sized to the word rather than the trailing space.
				ImVec2 p = ImGui::GetCursorScreenPos();
				ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(p.x - 2, p.y - 1),
				                                          ImVec2(p.x + word.x + 3, p.y + word.y + 2),
				                                          st.pal.code_bg, 4.0f);
			}
			ImGui::TextUnformatted(text);
			ImGui::PopStyleColor();

			ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
			if (sp.strike)
				dl->AddLine(ImVec2(a.x, (a.y + b.y) * 0.5f), ImVec2(a.x + word.x, (a.y + b.y) * 0.5f), col, 1.0f);
			if (!sp.url.empty()) {
				bool hot = ImGui::IsItemHovered();
				dl->AddLine(ImVec2(a.x, b.y - 1), ImVec2(a.x + word.x, b.y - 1), col, hot ? 1.6f : 1.0f);
				if (hot) {
					ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
					ImGui::SetTooltip("%s", sp.url.c_str());
					if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && st.on_link) st.on_link(sp.url);
				}
			}
			ImGui::PopFont();
			ImGui::SameLine(0, 0);
			// Keep the cursor honest about the trailing space we didn't draw.
			if (wrapped && full.x > word.x) ImGui::Dummy(ImVec2(full.x - word.x, 0)), ImGui::SameLine(0, 0);
			line_has_content = true;
		}
	}
	if (line_has_content) ImGui::NewLine();
}

void Render(const std::vector<Block> &blocks, const RenderStyle &st) {
	const float base = st.base_px;
	// PaperMod's heading scale, near enough: h1 down to h6 against 1rem body.
	const float h_scale[7] = {1.0f, 1.85f, 1.45f, 1.22f, 1.08f, 1.0f, 0.94f};
	ImDrawList *dl = ImGui::GetWindowDrawList();
	int block_no = 0;

	for (const Block &b : blocks) {
		ImGui::PushID(block_no++);
		switch (b.kind) {
		case BlockKind::Heading: {
			float px = base * h_scale[b.level < 1 ? 1 : (b.level > 6 ? 6 : b.level)];
			ImGui::Dummy(ImVec2(0, base * (b.level <= 2 ? 0.7f : 0.5f)));
			// h1/h2 sit above a hairline, the way the theme separates sections.
			ImGui::PushFont(Theme::F_BOLD, px);
			RenderSpans(b.spans, st, px, st.pal.primary);
			ImGui::PopFont();
			if (b.level <= 2) {
				ImVec2 p = ImGui::GetCursorScreenPos();
				float w = ImGui::GetContentRegionAvail().x;
				dl->AddLine(ImVec2(p.x, p.y + 3), ImVec2(p.x + w, p.y + 3), st.pal.border, 1.0f);
				ImGui::Dummy(ImVec2(0, 7));
			} else {
				ImGui::Dummy(ImVec2(0, 3));
			}
			break;
		}
		case BlockKind::Paragraph:
			RenderSpans(b.spans, st, base, st.pal.content);
			ImGui::Dummy(ImVec2(0, base * 0.55f));
			break;

		case BlockKind::List: {
			for (const ListItem &it : b.items) {
				float ind = 14.0f + it.indent * 20.0f;
				ImGui::Indent(ind);
				ImVec2 p = ImGui::GetCursorScreenPos();
				if (it.task) {
					// - [x] renders as a real checkbox plate, read-only here.
					ImU32 c = it.checked ? st.pal.link : st.pal.secondary;
					dl->AddRect(ImVec2(p.x - 14, p.y + 3), ImVec2(p.x - 2, p.y + 15), c, 2.0f, 0, 1.4f);
					if (it.checked) {
						dl->AddLine(ImVec2(p.x - 11, p.y + 9), ImVec2(p.x - 9, p.y + 12), c, 1.8f);
						dl->AddLine(ImVec2(p.x - 9, p.y + 12), ImVec2(p.x - 5, p.y + 5), c, 1.8f);
					}
				} else if (it.ordered) {
					ImGui::PushFont(Theme::F_UI, base);
					ImVec2 ms = ImGui::CalcTextSize(it.marker.c_str());
					dl->AddText(ImVec2(p.x - ms.x - 6, p.y), st.pal.secondary, it.marker.c_str());
					ImGui::PopFont();
				} else {
					dl->AddCircleFilled(ImVec2(p.x - 9, p.y + base * 0.55f), 2.6f, st.pal.secondary, 8);
				}
				RenderSpans(it.spans, st, base, st.pal.content);
				ImGui::Unindent(ind);
			}
			ImGui::Dummy(ImVec2(0, base * 0.5f));
			break;
		}

		case BlockKind::Code: {
			std::vector<std::string> lines = util::SplitLines(b.text);
			ImGui::PushFont(Theme::F_MONO, base * 0.92f);
			float lh = ImGui::GetTextLineHeightWithSpacing();
			float h = lh * (float)(lines.empty() ? 1 : lines.size()) + 16.0f;
			// Tall blocks scroll rather than pushing the rest of the post off
			// the bottom of the preview.
			float cap = ImGui::GetIO().DisplaySize.y * 0.5f;
			bool scroll = h > cap;
			if (scroll) h = cap;
			ImGui::PushStyleColor(ImGuiCol_ChildBg, Theme::V4(st.pal.code_block_bg));
			ImGui::PushStyleColor(ImGuiCol_Text, Theme::V4(st.pal.content));
			ImGui::BeginChild("code", ImVec2(0, h), ImGuiChildFlags_Borders,
			                  ImGuiWindowFlags_HorizontalScrollbar);
			for (const std::string &l : lines) ImGui::TextUnformatted(l.c_str());
			ImGui::EndChild();
			ImGui::PopStyleColor(2);
			ImGui::PopFont();

			// The language tag and a copy button, as PaperMod does with
			// ShowCodeCopyButtons.
			ImVec2 mn = ImGui::GetItemRectMin(), mx = ImGui::GetItemRectMax();
			if (!b.lang.empty()) {
				ImGui::PushFont(Theme::F_MONO, base * 0.72f);
				ImVec2 ts = ImGui::CalcTextSize(b.lang.c_str());
				dl->AddText(ImVec2(mx.x - ts.x - 10, mn.y + 5), st.pal.secondary, b.lang.c_str());
				ImGui::PopFont();
			}
			if (ImGui::IsMouseHoveringRect(mn, mx)) {
				ImGui::SetCursorScreenPos(ImVec2(mx.x - 68, mn.y + 6));
				ImGui::PushFont(Theme::F_UI, base * 0.78f);
				if (ImGui::Button("copy")) ImGui::SetClipboardText(b.text.c_str());
				ImGui::PopFont();
				ImGui::SetCursorScreenPos(ImVec2(mn.x, mx.y));
			}
			ImGui::Dummy(ImVec2(0, base * 0.6f));
			break;
		}

		case BlockKind::Quote: {
			ImVec2 p = ImGui::GetCursorScreenPos();
			ImGui::Indent(16.0f);
			float y0 = ImGui::GetCursorScreenPos().y;
			ImGui::PushFont(Theme::F_ITALIC, base);
			RenderSpans(b.spans, st, base, st.pal.secondary);
			ImGui::PopFont();
			float y1 = ImGui::GetCursorScreenPos().y;
			dl->AddRectFilled(ImVec2(p.x, y0 - 2), ImVec2(p.x + 3, y1 - 4), st.pal.tertiary, 1.5f);
			ImGui::Unindent(16.0f);
			ImGui::Dummy(ImVec2(0, base * 0.5f));
			break;
		}

		case BlockKind::Table: {
			if (b.rows.empty()) break;
			size_t cols = 0;
			for (const auto &r : b.rows) cols = r.size() > cols ? r.size() : cols;
			if (cols == 0) break;
			ImGui::PushStyleColor(ImGuiCol_TableBorderLight, Theme::V4(st.pal.border));
			ImGui::PushStyleColor(ImGuiCol_TableBorderStrong, Theme::V4(st.pal.border));
			if (ImGui::BeginTable("t", (int)cols,
			                      ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
			                          ImGuiTableFlags_SizingStretchSame)) {
				// Columns have to be declared, not just counted: without a
				// TableSetupColumn per column the cells came out ~30px wide
				// and every heading was clipped to three letters.
				for (size_t c = 0; c < cols; c++)
					ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthStretch, 1.0f);
				for (size_t r = 0; r < b.rows.size(); r++) {
					ImGui::TableNextRow();
					for (size_t c = 0; c < cols; c++) {
						ImGui::TableSetColumnIndex((int)c);
						std::string cell = c < b.rows[r].size() ? b.rows[r][c] : "";
						bool head = (r == 0 && b.has_header);
						ImGui::PushFont(head ? Theme::F_BOLD : Theme::F_UI, base * 0.95f);
						ImGui::PushStyleColor(ImGuiCol_Text, Theme::V4(head ? st.pal.primary : st.pal.content));
						ImGui::TextWrapped("%s", cell.c_str());
						ImGui::PopStyleColor();
						ImGui::PopFont();
					}
				}
				ImGui::EndTable();
			}
			ImGui::PopStyleColor(2);
			ImGui::Dummy(ImVec2(0, base * 0.6f));
			break;
		}

		case BlockKind::Rule: {
			ImGui::Dummy(ImVec2(0, base * 0.4f));
			ImVec2 p = ImGui::GetCursorScreenPos();
			dl->AddLine(ImVec2(p.x, p.y), ImVec2(p.x + ImGui::GetContentRegionAvail().x, p.y), st.pal.border, 1.0f);
			ImGui::Dummy(ImVec2(0, base * 0.8f));
			break;
		}

		case BlockKind::Html: {
			// unsafe = true is set in hugo.toml, so this really does reach the
			// page -- it is shown as-is, flagged, rather than silently hidden.
			ImGui::PushFont(Theme::F_MONO, base * 0.82f);
			ImGui::PushStyleColor(ImGuiCol_Text, Theme::V4(st.pal.secondary));
			ImGui::PushStyleColor(ImGuiCol_ChildBg, Theme::V4(st.pal.entry));
			float h = ImGui::GetTextLineHeightWithSpacing() * (float)util::SplitLines(b.text).size() + 14.0f;
			ImGui::BeginChild("html", ImVec2(0, h), ImGuiChildFlags_Borders);
			ImGui::TextUnformatted(util::TrimRight(b.text).c_str());
			ImGui::EndChild();
			ImGui::PopStyleColor(2);
			ImGui::PopFont();
			ImGui::Dummy(ImVec2(0, base * 0.5f));
			break;
		}

		case BlockKind::Shortcode: {
			ImGui::PushFont(Theme::F_MONO, base * 0.85f);
			ImVec2 p = ImGui::GetCursorScreenPos();
			ImVec2 ts = ImGui::CalcTextSize(b.text.c_str());
			dl->AddRectFilled(p, ImVec2(p.x + ts.x + 16, p.y + ts.y + 8), st.pal.entry, 5.0f);
			dl->AddRect(p, ImVec2(p.x + ts.x + 16, p.y + ts.y + 8), st.pal.tertiary, 5.0f);
			dl->AddText(ImVec2(p.x + 8, p.y + 4), st.pal.link, b.text.c_str());
			ImGui::Dummy(ImVec2(ts.x + 16, ts.y + 8));
			ImGui::PopFont();
			ImGui::SetItemTooltip("Hugo shortcode -- rendered by the theme, previewed here as-is");
			ImGui::Dummy(ImVec2(0, base * 0.5f));
			break;
		}
		}
		ImGui::PopID();
	}
}

} // namespace md
