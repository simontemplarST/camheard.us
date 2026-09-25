// Editor tab -- front matter as a form, the markdown source with a
// formatting toolbar, and a live preview painted in the site's own theme.
#include "app.h"
#include "util.h"
#include "widgets.h"

#include "imgui.h"
#include "imgui_stdlib.h"

#include <algorithm>

namespace {

// The front-matter keys the form owns. Everything else shows up in the
// "More fields" table rather than being hidden or dropped.
bool IsPrimaryKey(const std::string &k) {
	return k == "title" || k == "date" || k == "draft" || k == "summary" || k == "tags";
}

void ApplyAction(Editor &e, std::string *t, int *a, int *b) {
	const EditAction act = e.pending;
	const std::string &arg = e.pending_arg;
	switch (act) {
	case EditAction::Bold: md::WrapSelection(t, a, b, "**", "**"); break;
	case EditAction::Italic: md::WrapSelection(t, a, b, "*", "*"); break;
	case EditAction::InlineCode: md::WrapSelection(t, a, b, "`", "`"); break;
	case EditAction::Strike: md::WrapSelection(t, a, b, "~~", "~~"); break;
	case EditAction::H1: md::SetHeading(t, a, b, 1); break;
	case EditAction::H2: md::SetHeading(t, a, b, 2); break;
	case EditAction::H3: md::SetHeading(t, a, b, 3); break;
	case EditAction::Bullet: md::PrefixLines(t, a, b, "- "); break;
	case EditAction::Number: md::PrefixLines(t, a, b, "1. "); break;
	case EditAction::Quote: md::PrefixLines(t, a, b, "> "); break;
	case EditAction::CodeBlock: md::WrapSelection(t, a, b, "```\n", "\n```"); break;
	case EditAction::Link: md::MakeLink(t, a, b, arg.empty() ? "https://" : arg, false); break;
	case EditAction::Image: md::MakeLink(t, a, b, arg.empty() ? "/img/" : arg, true); break;
	case EditAction::Rule: md::InsertText(t, a, b, "\n---\n\n"); break;
	case EditAction::Table:
		md::InsertText(t, a, b, "\n| Column | Column |\n| --- | --- |\n|  |  |\n");
		break;
	// The find bar's three. Select changes no text at all -- it exists
	// because moving the caret from outside the widget doesn't survive
	// either, for exactly the reason editing it doesn't.
	case EditAction::Select:
		*a = e.pending_sel_a;
		*b = e.pending_sel_b;
		if (*a > (int)t->size()) *a = (int)t->size();
		if (*b > (int)t->size()) *b = (int)t->size();
		break;
	case EditAction::ReplaceCurrent:
		md::ReplaceOne(t, a, b, e.find_text, e.pending_repl, e.pending_case);
		break;
	case EditAction::ReplaceEvery:
		md::ReplaceAll(t, a, b, e.find_text, e.pending_repl, e.pending_case);
		break;
	case EditAction::None: break;
	}
}

// Runs every frame the text box is active. Two jobs: keep a copy of the
// selection (the toolbar needs it after focus has moved to a button), and
// apply a queued toolbar action to ImGui's own buffer -- which is the only
// safe place to change the text, see the comment on EditAction in app.h.
int EditorCallback(ImGuiInputTextCallbackData *data) {
	Editor *e = (Editor *)data->UserData;
	if (data->EventFlag != ImGuiInputTextFlags_CallbackAlways) return 0;

	if (e->pending != EditAction::None) {
		std::string t(data->Buf, (size_t)data->BufTextLen);
		const std::string before = t;
		int a = e->sel_a, b = e->sel_b;
		if (a > data->BufTextLen) a = data->BufTextLen;
		if (b > data->BufTextLen) b = data->BufTextLen;
		ApplyAction(*e, &t, &a, &b);
		// A jump to a match rewrites nothing, and must not mark the post
		// dirty or make ImGui rebuild the buffer for no reason.
		if (t != before) {
			data->DeleteChars(0, data->BufTextLen);
			data->InsertChars(0, t.c_str());
			e->dirty = true;
		}
		// Changing CursorPos from in here is also what scrolls the box to
		// the caret: ImGui sets CursorFollow when a callback moves it.
		data->CursorPos = b;
		data->SelectionStart = a;
		data->SelectionEnd = b;
		e->sel_a = a;
		e->sel_b = b;
		e->pending = EditAction::None;
		e->pending_arg.clear();
	} else {
		int a = data->SelectionStart, b = data->SelectionEnd;
		if (a > b) std::swap(a, b);
		e->sel_a = a;
		e->sel_b = b;
	}
	return 0;
}

void Queue(Editor &ed, EditAction act, const std::string &arg = "") {
	ed.pending = act;
	ed.pending_arg = arg;
	// The click moved focus to the button, so ask for it back: the action is
	// applied on the frame the text box becomes active again.
	ed.focus_editor = true;
}

void FrontMatterBar(App &a) {
	Editor &ed = a.ed;
	ImGui::PushStyleColor(ImGuiCol_ChildBg, Theme::V4(Theme::PANEL));
	// Height measured from the style rather than guessed at, so a font or
	// padding change can't clip the second row. NOT AutoResizeY: an
	// auto-fitting child reports its own (large) width back from
	// GetContentRegionAvail(), so every field sized against it was laid out
	// past the right edge and the bar came out empty.
	const ImGuiStyle &sty = ImGui::GetStyle();
	float fm_min = sty.WindowPadding.y * 2 + ImGui::GetTextLineHeight() + ImGui::GetFrameHeight() * 2 +
	               sty.ItemSpacing.y * 2 + 4;
	float fm_h = ed.fm_height > fm_min ? ed.fm_height : fm_min;
	ImGui::BeginChild("fm", ImVec2(0, fm_h), ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding);

	// Width reserved on the right of row one for draft + date + now + more.
	const float row1_right = 520.0f;
	const float row2_right = 560.0f;

	ImGui::PushFont(Theme::F_UI, Theme::UI_PX * 0.78f);
	ImGui::PushStyleColor(ImGuiCol_Text, Theme::V4(Theme::TEXT_FAINT));
	ImGui::TextUnformatted("TITLE");
	ImGui::PopStyleColor();
	ImGui::PopFont();

	std::string title = ed.doc.GetStr("title");
	float w1 = ImGui::GetContentRegionAvail().x - row1_right;
	if (w1 < 220) w1 = 220;
	ImGui::SetNextItemWidth(w1);
	ImGui::PushFont(Theme::F_BOLD, Theme::UI_PX * 1.1f);
	if (ImGui::InputText("##title", &title)) {
		ed.doc.SetStr("title", title);
		ed.dirty = true;
	}
	ImGui::PopFont();

	ImGui::SameLine();
	bool draft = ed.doc.GetBool("draft", false);
	if (ImGui::Checkbox("draft", &draft)) {
		ed.doc.SetBool("draft", draft);
		ed.dirty = true;
	}
	ImGui::SetItemTooltip("kept out of a normal `hugo` build; still shown in the local preview");

	ImGui::SameLine();
	std::string date = ed.doc.GetStr("date");
	ImGui::SetNextItemWidth(200);
	if (ImGui::InputTextWithHint("##date", "date", &date)) {
		ed.doc.SetStr("date", date);
		ed.dirty = true;
	}
	ImGui::SetItemTooltip("RFC3339, e.g. 2026-09-24T09:15:00-05:00");
	ImGui::SameLine();
	if (ImGui::Button("now")) {
		ed.doc.SetStr("date", util::NowRFC3339());
		ed.dirty = true;
	}
	ImGui::SetItemTooltip("stamp with the current local time");

	ImGui::SameLine();
	if (ImGui::Button("More fields")) ImGui::OpenPopup("morefields");
	ImGui::SetItemTooltip("every other key in this file's front matter");

	// ---- second line: summary + tags
	float w2 = ImGui::GetContentRegionAvail().x - row2_right;
	if (w2 < 220) w2 = 220;
	ImGui::SetNextItemWidth(w2);
	std::string summary = ed.doc.GetStr("summary");
	if (ImGui::InputTextWithHint("##summary", "summary -- shown in the post list and in link previews",
	                             &summary)) {
		ed.doc.SetStr("summary", summary);
		ed.dirty = true;
	}
	ImGui::SameLine();
	std::vector<std::string> tags = ed.doc.GetList("tags");
	if (W::ChipEditor("tags", &tags, &ed.tag_input, a.AllTags())) {
		ed.doc.SetList("tags", tags);
		ed.dirty = true;
	}

	// What the two rows actually needed, for the next frame's height.
	ed.fm_height = ImGui::GetCursorPosY() + sty.WindowPadding.y;

	// ---- everything else
	if (ImGui::BeginPopup("morefields")) {
		ImGui::PushFont(Theme::F_BOLD, Theme::UI_PX);
		ImGui::TextUnformatted("Other front matter");
		ImGui::PopFont();
		ImGui::PushStyleColor(ImGuiCol_Text, Theme::V4(Theme::TEXT_FAINT));
		ImGui::TextWrapped("Every key in this file that the bar above doesn't cover. Nothing here is "
		                   "invented or dropped on save -- keys arrive and leave in the order they are "
		                   "written.");
		ImGui::PopStyleColor();
		ImGui::Dummy(ImVec2(0, 6));

		if (ImGui::BeginTable("extra", 3, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_BordersInnerH)) {
			ImGui::TableSetupColumn("key", ImGuiTableColumnFlags_WidthStretch, 1.0f);
			ImGui::TableSetupColumn("value", ImGuiTableColumnFlags_WidthStretch, 2.2f);
			ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 30.0f);
			std::string remove_key;
			for (size_t i = 0; i < ed.doc.fields.size(); i++) {
				fm::Field &f = ed.doc.fields[i];
				if (IsPrimaryKey(f.key)) continue;
				ImGui::TableNextRow();
				ImGui::PushID((int)i);
				ImGui::TableSetColumnIndex(0);
				ImGui::AlignTextToFramePadding();
				ImGui::TextUnformatted(f.key.c_str());
				ImGui::TableSetColumnIndex(1);
				ImGui::SetNextItemWidth(-1);
				if (f.v.type == fm::Type::Bool) {
					bool b = f.v.b;
					if (ImGui::Checkbox("##v", &b)) {
						f.v.b = b;
						ed.dirty = true;
					}
				} else if (f.v.type == fm::Type::List) {
					std::string joined = util::Join(f.v.list, ", ");
					if (ImGui::InputText("##v", &joined)) {
						f.v.list.clear();
						std::string cur;
						for (char c : joined + ",") {
							if (c == ',') {
								std::string t = util::Trim(cur);
								if (!t.empty()) f.v.list.push_back(t);
								cur.clear();
							} else cur += c;
						}
						ed.dirty = true;
					}
				} else {
					std::string s = ed.doc.GetStr(f.key);
					if (ImGui::InputText("##v", &s)) {
						if (f.v.type == fm::Type::Num) f.v.n = atof(s.c_str());
						else f.v.s = s;
						ed.dirty = true;
					}
				}
				ImGui::TableSetColumnIndex(2);
				if (ImGui::SmallButton("x")) remove_key = f.key;
				ImGui::SetItemTooltip("remove this key");
				ImGui::PopID();
			}
			ImGui::EndTable();
			if (!remove_key.empty()) {
				ed.doc.Remove(remove_key);
				ed.dirty = true;
			}
		}

		ImGui::Dummy(ImVec2(0, 6));
		ImGui::SetNextItemWidth(160);
		ImGui::InputTextWithHint("##newkey", "new key", &ed.new_key);
		ImGui::SameLine();
		if (ImGui::Button("add") && !ed.new_key.empty()) {
			ed.doc.SetStr(ed.new_key, "");
			ed.new_key.clear();
			ed.dirty = true;
		}
		ImGui::SameLine();
		// The handful PaperMod actually reads, so they can be added without
		// looking them up.
		if (ImGui::BeginCombo("##known", "known keys", ImGuiComboFlags_WidthFitPreview)) {
			struct K { const char *key; const char *what; bool boolean; };
			static const K known[] = {
			    {"url", "override the published path", false},
			    {"layout", "a specific layout file, e.g. single", false},
			    {"weight", "pin to the top of a list (lower = earlier)", false},
			    {"ShowToc", "table of contents on this page", true},
			    {"TocOpen", "start the table of contents expanded", true},
			    {"hidemeta", "hide date/reading time on this page", true},
			    {"ShowReadingTime", "reading time on this page", true},
			    {"searchHidden", "keep out of the search index", true},
			    {"cover.image", "social/preview image for this page", false},
			    {"aliases", "old URLs that should redirect here", false},
			};
			for (const K &k : known) {
				if (ImGui::Selectable(k.key)) {
					if (k.boolean) ed.doc.SetBool(k.key, true);
					else ed.doc.SetStr(k.key, "");
					ed.dirty = true;
				}
				ImGui::SetItemTooltip("%s", k.what);
			}
			ImGui::EndCombo();
		}
		ImGui::EndPopup();
	}

	ImGui::EndChild();
	ImGui::PopStyleColor();
}

void Toolbar(App &a) {
	Editor &ed = a.ed;
	struct B { const char *label; const char *tip; EditAction act; };
	static const B group1[] = {
	    {"B", "bold  (Ctrl+B)", EditAction::Bold},
	    {"I", "italic  (Ctrl+I)", EditAction::Italic},
	    {"</>", "inline code", EditAction::InlineCode},
	    {"S", "strikethrough", EditAction::Strike},
	};
	static const B group2[] = {
	    {"H1", "heading 1", EditAction::H1},
	    {"H2", "heading 2", EditAction::H2},
	    {"H3", "heading 3", EditAction::H3},
	};
	static const B group3[] = {
	    {"list", "bullet list", EditAction::Bullet},
	    {"1.", "numbered list", EditAction::Number},
	    {"quote", "block quote", EditAction::Quote},
	    {"code", "fenced code block", EditAction::CodeBlock},
	    {"table", "insert a table skeleton", EditAction::Table},
	    {"hr", "horizontal rule", EditAction::Rule},
	};

	for (const B &b : group1) {
		if (W::ToolButton(b.label, b.tip)) Queue(ed, b.act);
		ImGui::SameLine(0, 3);
	}
	ImGui::SameLine(0, 12);
	for (const B &b : group2) {
		if (W::ToolButton(b.label, b.tip)) Queue(ed, b.act);
		ImGui::SameLine(0, 3);
	}
	ImGui::SameLine(0, 12);
	for (const B &b : group3) {
		if (W::ToolButton(b.label, b.tip)) Queue(ed, b.act);
		ImGui::SameLine(0, 3);
	}

	ImGui::SameLine(0, 12);
	if (W::ToolButton("link", "insert a link  (Ctrl+K)")) {
		ed.link_url = "https://";
		ImGui::OpenPopup("linkpopup");
	}
	ImGui::SameLine(0, 3);
	if (W::ToolButton("image", "insert an image reference")) {
		ed.link_url = "/img/";
		ImGui::OpenPopup("imagepopup");
	}
	// Where the buttons end, in window-local x. Read here because by the
	// time the word count is drawn the cursor has already wrapped to the
	// next line, and comparing against that says there is room when there
	// isn't.
	const float toolbar_end = ImGui::GetItemRectMax().x - ImGui::GetWindowPos().x;

	if (ImGui::BeginPopup("linkpopup")) {
		ImGui::TextUnformatted("Link URL");
		ImGui::SetNextItemWidth(340);
		if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
		bool go = ImGui::InputText("##url", &ed.link_url, ImGuiInputTextFlags_EnterReturnsTrue);
		ImGui::SameLine();
		go |= ImGui::Button("Insert");
		ImGui::PushStyleColor(ImGuiCol_Text, Theme::V4(Theme::TEXT_FAINT));
		ImGui::TextUnformatted("The selected text becomes the label; with nothing selected you get a\n"
		                       "placeholder to type over.");
		ImGui::PopStyleColor();
		if (go) {
			Queue(ed, EditAction::Link, ed.link_url);
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndPopup();
	}
	if (ImGui::BeginPopup("imagepopup")) {
		ImGui::TextUnformatted("Image path");
		ImGui::SetNextItemWidth(340);
		if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
		bool go = ImGui::InputText("##img", &ed.link_url, ImGuiInputTextFlags_EnterReturnsTrue);
		ImGui::SameLine();
		go |= ImGui::Button("Insert");
		// Anything already in static/ is one click away.
		if (!a.site.media.empty() && ImGui::BeginCombo("##pick", "from static/", ImGuiComboFlags_WidthFitPreview)) {
			for (const site::MediaFile &m : a.site.media) {
				if (!m.image) continue;
				if (ImGui::Selectable(m.web.c_str())) ed.link_url = m.web;
			}
			ImGui::EndCombo();
		}
		ImGui::PushStyleColor(ImGuiCol_Text, Theme::V4(Theme::TEXT_FAINT));
		ImGui::TextUnformatted("Files in static/ are served from the site root: static/img/a.png is /img/a.png.\n"
		                       "You can also drag a file onto this window to copy it in and insert it.");
		ImGui::PopStyleColor();
		if (go) {
			Queue(ed, EditAction::Image, ed.link_url);
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndPopup();
	}

	// Word count, right-aligned.
	char buf[96];
	snprintf(buf, sizeof buf, "%d words  ·  ~%d min read", fm::WordCount(ed.body),
	         fm::WordCount(ed.body) / 200 + 1);
	ImGui::PushFont(Theme::F_UI, Theme::UI_PX * 0.82f);
	float w = ImGui::CalcTextSize(buf).x;
	float at = ImGui::GetContentRegionMax().x - w - 4;
	// Only when it fits: with the outline rail open the toolbar can reach
	// this far, and a word count printed on top of the image button is
	// worse than no word count.
	if (at > toolbar_end + 12) {
		ImGui::SameLine(at);
		ImGui::PushStyleColor(ImGuiCol_Text, Theme::V4(Theme::TEXT_FAINT));
		ImGui::AlignTextToFramePadding();
		ImGui::TextUnformatted(buf);
		ImGui::PopStyleColor();
	} else {
		ImGui::NewLine();
	}
	ImGui::PopFont();
}

// The find bar (Ctrl+F). It sits under the front-matter bar rather than
// floating over the text: a search box that covers the thing being searched
// is a classic, and there is room for a real one here.
//
// Every jump and every replacement goes through the same queue the toolbar
// uses, for the same reason -- see EditAction in app.h. The extra wrinkle is
// focus: applying the action needs the text box active, so focus is taken
// for a frame and handed straight back, or the next thing typed into the
// find box would land in the post.
void FindBar(App &a) {
	Editor &ed = a.ed;
	std::vector<int> hits = md::FindAll(ed.body, ed.find_text, ed.find_case);
	int cur = -1;
	for (int i = 0; i < (int)hits.size(); i++)
		if (hits[i] == ed.find_at) cur = i;

	auto jump_to = [&](int idx) {
		if (hits.empty()) return;
		idx = (idx % (int)hits.size() + (int)hits.size()) % (int)hits.size();
		ed.find_at = hits[idx];
		ed.pending_sel_a = hits[idx];
		ed.pending_sel_b = hits[idx] + (int)ed.find_text.size();
		ed.pending = EditAction::Select;
		ed.focus_editor = true;
		ed.refocus_find = true;
	};
	auto step = [&](bool back) {
		if (hits.empty()) return;
		if (cur >= 0) {
			jump_to(cur + (back ? -1 : 1));
			return;
		}
		// Nothing current: start from wherever the caret is, and wrap.
		if (back) {
			int pick = (int)hits.size() - 1;
			for (int i = (int)hits.size() - 1; i >= 0; i--)
				if (hits[i] < ed.sel_a) { pick = i; break; }
			jump_to(pick);
		} else {
			int pick = 0;
			for (int i = 0; i < (int)hits.size(); i++)
				if (hits[i] >= ed.sel_a) { pick = i; break; }
			jump_to(pick);
		}
	};

	ImGui::PushStyleColor(ImGuiCol_ChildBg, Theme::V4(Theme::PANEL));
	ImGui::BeginChild("findbar", ImVec2(0, ImGui::GetFrameHeight() + 16),
	                  ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding,
	                  ImGuiWindowFlags_NoScrollbar);

	if (ed.focus_find) {
		ImGui::SetKeyboardFocusHere();
		ed.focus_find = false;
	}
	ImGui::SetNextItemWidth(260);
	bool enter = ImGui::InputTextWithHint("##find", "find", &ed.find_text,
	                                      ImGuiInputTextFlags_EnterReturnsTrue);
	if (enter) step(ImGui::GetIO().KeyShift);

	ImGui::SameLine();
	if (W::ToolButton("Aa", "match case", ed.find_case)) {
		ed.find_case = !ed.find_case;
		ed.find_at = -1;
	}
	ImGui::SameLine();
	ImGui::BeginDisabled(hits.empty());
	if (W::ToolButton("<", "previous match  (Shift+Enter)")) step(true);
	ImGui::SameLine(0, 3);
	if (W::ToolButton(">", "next match  (Enter)")) step(false);
	ImGui::EndDisabled();

	ImGui::SameLine();
	ImGui::PushStyleColor(ImGuiCol_Text,
	                      Theme::V4(ed.find_text.empty() ? Theme::TEXT_FAINT
	                                                     : (hits.empty() ? Theme::DANGER : Theme::TEXT_DIM)));
	ImGui::AlignTextToFramePadding();
	if (ed.find_text.empty()) ImGui::TextUnformatted("       ");
	else if (hits.empty()) ImGui::TextUnformatted("no matches");
	else if (cur >= 0) ImGui::Text("%d of %d", cur + 1, (int)hits.size());
	else ImGui::Text("%d match%s", (int)hits.size(), hits.size() == 1 ? "" : "es");
	ImGui::PopStyleColor();

	ImGui::SameLine(0, 18);
	ImGui::SetNextItemWidth(260);
	ImGui::InputTextWithHint("##replace", "replace with", &ed.replace_text);
	ImGui::SameLine();
	// Replace acts on the match the caret is sitting on, so it can never
	// quietly eat a different piece of text than the one being looked at.
	bool on_match = (cur >= 0 && ed.sel_a == ed.find_at &&
	                 ed.sel_b == ed.find_at + (int)ed.find_text.size());
	ImGui::BeginDisabled(!on_match);
	if (ImGui::Button("Replace")) {
		ed.pending = EditAction::ReplaceCurrent;
		ed.pending_repl = ed.replace_text;
		ed.pending_case = ed.find_case;
		ed.find_at = -1;
		ed.focus_editor = true;
		ed.refocus_find = true;
	}
	ImGui::EndDisabled();
	if (!on_match) ImGui::SetItemTooltip("jump to a match first (Enter)");
	ImGui::SameLine();
	ImGui::BeginDisabled(hits.empty());
	if (W::DangerButton("Replace all")) {
		int n = (int)hits.size();
		ed.pending = EditAction::ReplaceEvery;
		ed.pending_repl = ed.replace_text;
		ed.pending_case = ed.find_case;
		ed.find_at = -1;
		ed.focus_editor = true;
		ed.refocus_find = true;
		a.Notify("replaced " + std::to_string(n) + " occurrence" + (n == 1 ? "" : "s"));
	}
	ImGui::EndDisabled();

	ImGui::SameLine(ImGui::GetContentRegionMax().x - 30);
	if (ImGui::Button("x", ImVec2(24, 0))) ed.find_open = false;
	ImGui::SetItemTooltip("close the find bar (Esc)");

	ImGui::EndChild();
	ImGui::PopStyleColor();

	// Esc closes it from either box.
	if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
		ed.find_open = false;
		ed.focus_editor = true;
	}
}

// The headings rail. A long post is navigated by its shape, and scrolling
// for it is the one thing the split view makes harder rather than easier.
void OutlinePane(App &a, const ImVec2 &size) {
	Editor &ed = a.ed;
	ImGui::BeginChild("outlinepane", size, ImGuiChildFlags_Borders);
	ImGui::PushFont(Theme::F_UI, Theme::UI_PX * 0.78f);
	ImGui::PushStyleColor(ImGuiCol_Text, Theme::V4(Theme::TEXT_FAINT));
	ImGui::TextUnformatted("OUTLINE");
	ImGui::PopStyleColor();
	ImGui::PopFont();

	std::vector<md::Heading> hs = md::Outline(ed.body);
	if (hs.empty()) {
		ImGui::PushStyleColor(ImGuiCol_Text, Theme::V4(Theme::TEXT_FAINT));
		ImGui::TextWrapped("No headings yet. The H1/H2/H3 buttons add them.");
		ImGui::PopStyleColor();
		ImGui::EndChild();
		return;
	}
	// Which heading the caret is under, so the rail says where you are.
	int current = -1;
	for (int i = 0; i < (int)hs.size(); i++)
		if (hs[i].offset <= ed.sel_a) current = i;

	for (int i = 0; i < (int)hs.size(); i++) {
		const md::Heading &h = hs[i];
		float indent = (h.level - 1) * 12.0f;
		if (indent > 0) ImGui::Indent(indent);
		ImGui::PushID(i);
		ImGui::PushFont(h.level == 1 ? Theme::F_BOLD : Theme::F_UI, Theme::UI_PX * 0.88f);
		ImGui::PushStyleColor(ImGuiCol_Text, Theme::V4(i == current ? Theme::TEXT : Theme::TEXT_DIM));
		if (ImGui::Selectable(h.text.empty() ? "(untitled)" : h.text.c_str(), i == current)) {
			ed.pending_sel_a = ed.pending_sel_b = h.offset;
			ed.pending = EditAction::Select;
			ed.focus_editor = true;
		}
		ImGui::PopStyleColor();
		ImGui::PopFont();
		ImGui::SetItemTooltip("line %d", h.line + 1);
		ImGui::PopID();
		if (indent > 0) ImGui::Unindent(indent);
	}
	ImGui::EndChild();
}

void SourcePane(App &a, const ImVec2 &size) {
	Editor &ed = a.ed;
	ImGui::BeginChild("source", size, ImGuiChildFlags_None);
	Toolbar(a);

	// Ctrl shortcuts, live while the text box has focus.
	ImGuiIO &io = ImGui::GetIO();
	if (io.KeyCtrl) {
		if (ImGui::IsKeyPressed(ImGuiKey_B, false)) Queue(ed, EditAction::Bold);
		if (ImGui::IsKeyPressed(ImGuiKey_I, false)) Queue(ed, EditAction::Italic);
		if (ImGui::IsKeyPressed(ImGuiKey_K, false)) {
			ed.link_url = "https://";
			ImGui::OpenPopup("linkpopup");
		}
		if (ImGui::IsKeyPressed(ImGuiKey_F, false)) {
			ed.find_open = true;
			ed.focus_find = true;
		}
	}

	if (ed.focus_editor) {
		ImGui::SetKeyboardFocusHere();
		ed.focus_editor = false;
	}
	ImGui::PushFont(Theme::F_MONO, Theme::UI_PX * 0.95f);
	ImVec2 avail = ImGui::GetContentRegionAvail();
	if (ImGui::InputTextMultiline("##body", &ed.body, avail,
	                              ImGuiInputTextFlags_AllowTabInput | ImGuiInputTextFlags_CallbackAlways,
	                              EditorCallback, &ed))
		ed.dirty = true;
	ImGui::PopFont();
	ImGui::EndChild();
}

void PreviewPane(App &a, const ImVec2 &size) {
	Editor &ed = a.ed;
	Paper::Palette pal = Paper::Get(ed.preview_dark);
	ImGui::PushStyleColor(ImGuiCol_ChildBg, Theme::V4(pal.theme));
	ImGui::BeginChild("preview", size, ImGuiChildFlags_Borders);

	// A centred column, the way --main-width does on the real page.
	float w = ImGui::GetContentRegionAvail().x;
	float col = w > 760 ? 720 : w - 24;
	float pad = (w - col) * 0.5f;
	if (pad > 0) ImGui::Indent(pad);
	ImGui::PushTextWrapPos(pad + col);

	std::string title = ed.doc.GetStr("title");
	if (!title.empty()) {
		ImGui::Dummy(ImVec2(0, 10));
		ImGui::PushFont(Theme::F_BOLD, Theme::UI_PX * 2.0f);
		ImGui::PushStyleColor(ImGuiCol_Text, Theme::V4(pal.primary));
		ImGui::TextWrapped("%s", title.c_str());
		ImGui::PopStyleColor();
		ImGui::PopFont();

		// PaperMod's meta line, driven by the same params as the real page.
		std::string meta;
		if (!a.cfg.GetBool("params.hidemeta", false)) {
			if (!ed.doc.GetStr("date").empty()) meta += util::PrettyDate(ed.doc.GetStr("date")).substr(0, 10);
			int words = fm::WordCount(ed.body);
			if (a.cfg.GetBool("params.ShowReadingTime", true)) {
				if (!meta.empty()) meta += " · ";
				meta += std::to_string(words / 200 + 1) + " min";
			}
			if (a.cfg.GetBool("params.ShowWordCount", false)) {
				if (!meta.empty()) meta += " · ";
				meta += std::to_string(words) + " words";
			}
		}
		if (ed.doc.GetBool("draft", false)) meta += meta.empty() ? "DRAFT" : " · DRAFT";
		if (!meta.empty()) {
			ImGui::PushFont(Theme::F_UI, Theme::UI_PX * 0.82f);
			ImGui::PushStyleColor(ImGuiCol_Text, Theme::V4(pal.secondary));
			ImGui::TextUnformatted(meta.c_str());
			ImGui::PopStyleColor();
			ImGui::PopFont();
		}
		ImGui::Dummy(ImVec2(0, 14));
	}

	md::RenderStyle st = a.PreviewStyle(ed.preview_dark);
	st.wrap_x = pad + col;
	md::Render(a.Blocks(), st);

	if (!ed.doc.GetList("tags").empty()) {
		ImGui::Dummy(ImVec2(0, 10));
		for (const std::string &t : ed.doc.GetList("tags")) {
			W::Pill(("#" + t).c_str(), pal.entry, pal.secondary);
			ImGui::SameLine();
		}
		ImGui::NewLine();
	}
	ImGui::Dummy(ImVec2(0, 40));
	ImGui::PopTextWrapPos();
	if (pad > 0) ImGui::Unindent(pad);
	ImGui::EndChild();
	ImGui::PopStyleColor();
}

} // namespace

void DrawEditorTab(App &a) {
	Editor &ed = a.ed;
	if (ed.path.empty()) {
		ImGui::Dummy(ImVec2(0, 60));
		ImGui::Indent(40);
		ImGui::PushFont(Theme::F_BOLD, Theme::UI_PX * 1.4f);
		ImGui::TextUnformatted("Nothing open");
		ImGui::PopFont();
		ImGui::PushStyleColor(ImGuiCol_Text, Theme::V4(Theme::TEXT_DIM));
		ImGui::TextWrapped("Pick a page on the Content tab, or start a new one.");
		ImGui::PopStyleColor();
		ImGui::Dummy(ImVec2(0, 12));
		if (W::PrimaryButton("New post", ImVec2(160, 0))) a.want_new_post = true;
		ImGui::SameLine();
		if (ImGui::Button("Browse content", ImVec2(160, 0))) a.tab = Tab::Content;
		ImGui::Unindent(40);
		return;
	}

	// ---- header row
	ImGui::PushFont(Theme::F_MONO, Theme::UI_PX * 0.85f);
	ImGui::PushStyleColor(ImGuiCol_Text, Theme::V4(Theme::TEXT_DIM));
	ImGui::AlignTextToFramePadding();
	std::string rel = ed.path;
	if (util::StartsWith(rel, a.site.root)) rel = rel.substr(a.site.root.size() + 1);
	ImGui::TextUnformatted(rel.c_str());
	ImGui::PopStyleColor();
	ImGui::PopFont();
	if (ed.dirty) {
		ImGui::SameLine();
		ImGui::PushStyleColor(ImGuiCol_Text, Theme::V4(Theme::WARN));
		ImGui::TextUnformatted("· unsaved");
		ImGui::PopStyleColor();
	}

	float right = ImGui::GetContentRegionMax().x;
	auto right_button = [&](const char *label, float w) {
		right -= w;
		ImGui::SameLine(right);
		right -= 6;
		return ImGui::Button(label, ImVec2(w, 0));
	};

	if (right_button("Close", 74)) {
		if (ed.dirty) a.SavePost();
		a.CloseEditor();
		a.tab = Tab::Content;
		return;
	}
	if (right_button("Preview in browser", 168)) {
		site::Post *p = a.site.Find(ed.path);
		if (ed.dirty) a.SavePost();
		if (!a.server.Running()) {
			a.StartServer();
			a.Notify("server starting -- the page will be there in a second");
		}
		if (p) util::OpenURL(a.LocalURL(*p));
	}
	ImGui::SetItemTooltip("saves, then opens this page in the local Hugo server -- the authoritative render");

	right -= 110;
	ImGui::SameLine(right);
	right -= 6;
	ImGui::BeginDisabled(!ed.dirty);
	if (W::PrimaryButton("Save", ImVec2(110, 0))) a.SavePost();
	ImGui::EndDisabled();

	// View mode, preview theme, and the two panes that can be toggled.
	const char *modes[] = {"Split", "Source", "Preview"};
	right -= 372;
	ImGui::SameLine(right);
	for (int i = 0; i < 3; i++) {
		if (W::ToolButton(modes[i], nullptr, ed.view == i)) ed.view = i;
		ImGui::SameLine(0, 3);
	}
	if (W::ToolButton(ed.preview_dark ? "dark" : "light", "preview in the site's dark or light theme"))
		ed.preview_dark = !ed.preview_dark;
	ImGui::SameLine(0, 10);
	if (W::ToolButton("outline", "the headings in this post", ed.show_outline))
		ed.show_outline = !ed.show_outline;
	ImGui::SameLine(0, 3);
	if (W::ToolButton("find", "find and replace  (Ctrl+F)", ed.find_open)) {
		ed.find_open = !ed.find_open;
		ed.focus_find = ed.find_open;
	}
	ImGui::NewLine();

	FrontMatterBar(a);
	if (ed.find_open) FindBar(a);

	ImVec2 avail = ImGui::GetContentRegionAvail();
	if (ed.show_outline) {
		const float rail = 220.0f;
		OutlinePane(a, ImVec2(rail, avail.y));
		ImGui::SameLine(0, 10);
		avail.x -= rail + 10;
	}
	if (ed.view == 1) {
		SourcePane(a, avail);
	} else if (ed.view == 2) {
		PreviewPane(a, avail);
	} else {
		float half = (avail.x - 10) * 0.5f;
		SourcePane(a, ImVec2(half, avail.y));
		ImGui::SameLine(0, 10);
		PreviewPane(a, ImVec2(avail.x - half - 10, avail.y));
	}

	// The find bar borrowed focus so its action could be applied inside the
	// text box's callback; now that it has been, hand focus back.
	if (ed.refocus_find && ed.pending == EditAction::None) {
		ed.refocus_find = false;
		ed.focus_find = true;
	}
}
