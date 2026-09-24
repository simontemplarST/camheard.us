// hugofe -- a Dear ImGui front end for a Hugo site.
//
//   ./hugofe [site-dir] [--screenshot DIR]
//
// With no argument it walks up from the working directory looking for a
// hugo.toml next to a content/ directory, so running it from gui/ finds the
// site above. --screenshot drives every tab and writes a PNG of each, which
// is how the UI gets looked at rather than merely compiled.
#include "app.h"
#include "shot.h"
#include "util.h"
#include "widgets.h"

#include "imgui.h"
#include "imgui_impl_opengl3.h"
#include "imgui_impl_sdl2.h"
#include "imgui_stdlib.h"

#include <SDL.h>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <unistd.h>

// ---------------------------------------------------------------- App methods

void App::Notify(const std::string &msg, bool bad) {
	toast = msg;
	toast_bad = bad;
	toast_until = ImGui::GetTime() + (bad ? 7.0 : 4.0);
}

void App::ReloadConfig() {
	cfg.Load(site.config);
	util::ReadFile(site.config, &cfg_text_on_disk);
	cfg_dirty = false;
}

void App::ReloadAll() {
	std::string keep = ed.path;
	site.Scan();
	site.ScanMedia();
	ReloadConfig();
	selected = -1;
	for (size_t i = 0; i < site.posts.size(); i++)
		if (site.posts[i].path == keep) selected = (int)i;
}

bool App::OpenPost(const std::string &path) {
	std::string text;
	if (!util::ReadFile(path, &text)) {
		Notify("could not read " + util::BaseName(path), true);
		return false;
	}
	ed.doc = fm::Parse(text);
	ed.body = ed.doc.body;
	ed.path = path;
	ed.saved_snapshot = text;
	ed.dirty = false;
	ed.sel_a = ed.sel_b = 0;
	ed.blocks_of = "\x01none";  // force a reparse
	ed.status.clear();
	ed.preview_dark = cfg.GetString("params.defaultTheme", "dark") != "light";
	tab = Tab::Editor;
	return true;
}

bool App::SavePost() {
	if (ed.path.empty()) return false;
	ed.doc.body = ed.body;
	std::string text = fm::Serialize(ed.doc);
	if (!util::WriteFileAtomic(ed.path, text)) {
		Notify("could not write " + util::BaseName(ed.path), true);
		return false;
	}
	ed.saved_snapshot = text;
	ed.dirty = false;
	ReloadAll();
	Notify("saved " + util::BaseName(ed.path));
	return true;
}

bool App::SaveConfig() {
	if (!cfg.Save(site.config)) {
		Notify("could not write hugo.toml", true);
		return false;
	}
	util::ReadFile(site.config, &cfg_text_on_disk);
	cfg_dirty = false;
	Notify("saved hugo.toml");
	return true;
}

void App::CloseEditor() {
	ed.path.clear();
	ed.body.clear();
	ed.doc = fm::Doc{};
	ed.dirty = false;
	ed.blocks.clear();
	ed.blocks_of.clear();
}

const std::vector<md::Block> &App::Blocks() {
	if (ed.blocks_of != ed.body) {
		ed.blocks = md::ParseBlocks(ed.body);
		ed.blocks_of = ed.body;
	}
	return ed.blocks;
}

md::RenderStyle App::PreviewStyle(bool dark) const {
	md::RenderStyle st;
	st.pal = Paper::Get(dark);
	st.base_px = Theme::UI_PX;
	std::string root = site.root;
	st.image_exists = [root](const std::string &src) -> bool {
		if (src.empty()) return false;
		if (util::StartsWith(src, "http://") || util::StartsWith(src, "https://") ||
		    util::StartsWith(src, "data:"))
			return true;
		std::string rel = src[0] == '/' ? src.substr(1) : src;
		// Hugo serves static/ at the root and also publishes page bundle
		// resources next to the page, so both count as "there".
		return util::Exists(root + "/static/" + rel) || util::Exists(root + "/assets/" + rel) ||
		       util::Exists(root + "/content/" + rel);
	};
	st.on_link = [](const std::string &url) {
		if (util::StartsWith(url, "http://") || util::StartsWith(url, "https://")) util::OpenURL(url);
	};
	return st;
}

std::string App::BaseURL() const { return cfg.GetString("baseURL", ""); }

std::string App::LocalURL(const site::Post &p) const {
	return "http://localhost:" + std::to_string(server_port) + p.permalink;
}

void App::StartServer() {
	if (server.Running()) {
		server.Stop();
		Notify("preview server stopped");
		return;
	}
	std::string err;
	// -D/-E/-F: drafts, expired and future-dated posts all render, which is
	// the whole point of a local preview -- a new post is a draft.
	std::vector<std::string> argv = {"hugo",   "server", "-D",   "-E",
	                                 "-F",     "--bind", "127.0.0.1", "--port",
	                                 std::to_string(server_port)};
	if (!server.Start(argv, site.root, &err)) {
		Notify("could not start hugo server: " + err, true);
		return;
	}
	Notify("preview server starting on port " + std::to_string(server_port));
}

void App::RunTask(const std::vector<std::string> &argv) {
	if (task.Running()) {
		Notify("another job is still running", true);
		return;
	}
	std::string err;
	if (!task.Start(argv, site.root, &err)) Notify("could not run " + argv[0] + ": " + err, true);
}

std::vector<std::string> App::AllTags() const {
	std::vector<std::string> out;
	for (const auto &p : site.posts)
		for (const auto &t : p.tags)
			if (std::find(out.begin(), out.end(), t) == out.end()) out.push_back(t);
	std::sort(out.begin(), out.end());
	return out;
}

// ------------------------------------------------------------------- chrome

namespace {

struct TabDef {
	const char *label;
	const char *hint;
};
const TabDef kTabs[(int)Tab::COUNT] = {
    {"Content", "every page and post in content/"},
    {"Editor", "write, with a live preview"},
    {"Design", "the site's own UI: header, home page, menus, post options"},
    {"Media", "files under static/ and assets/"},
    {"Publish", "build, preview, commit, push"},
};

void DrawTopBar(App &a) {
	const float h = 54.0f;
	ImGui::PushStyleColor(ImGuiCol_ChildBg, Theme::V4(Theme::RAIL));
	ImGui::BeginChild("topbar", ImVec2(0, h), ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar);
	ImGui::SetCursorPos(ImVec2(16, 9));

	ImGui::PushFont(Theme::F_BOLD, Theme::UI_PX * 1.15f);
	ImGui::PushStyleColor(ImGuiCol_Text, Theme::V4(Theme::ACCENT));
	ImGui::TextUnformatted("hugofe");
	ImGui::PopStyleColor();
	ImGui::PopFont();
	ImGui::SameLine();
	ImGui::PushFont(Theme::F_UI, Theme::UI_PX * 0.8f);
	ImGui::PushStyleColor(ImGuiCol_Text, Theme::V4(Theme::TEXT_FAINT));
	ImGui::SetCursorPosY(14);
	ImGui::TextUnformatted(a.cfg.GetString("title", util::BaseName(a.site.root)).c_str());
	ImGui::PopStyleColor();
	ImGui::PopFont();

	ImGui::SameLine(0, 26);
	ImGui::SetCursorPosY(8);
	for (int i = 0; i < (int)Tab::COUNT; i++) {
		bool active = (int)a.tab == i;
		if (active) {
			ImGui::PushStyleColor(ImGuiCol_Button, Theme::V4(Theme::PANEL_HI));
			ImGui::PushStyleColor(ImGuiCol_Text, Theme::V4(Theme::TEXT));
		} else {
			ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
			ImGui::PushStyleColor(ImGuiCol_Text, Theme::V4(Theme::TEXT_DIM));
		}
		if (ImGui::Button(kTabs[i].label, ImVec2(0, 34))) a.tab = (Tab)i;
		ImGui::SetItemTooltip("%s", kTabs[i].hint);
		ImGui::PopStyleColor(2);
		if (active) {
			ImVec2 mn = ImGui::GetItemRectMin(), mx = ImGui::GetItemRectMax();
			ImGui::GetWindowDrawList()->AddLine(ImVec2(mn.x + 4, mx.y + 1), ImVec2(mx.x - 4, mx.y + 1),
			                                    Theme::ACCENT, 2.0f);
		}
		ImGui::SameLine(0, 2);
	}

	// Right-aligned actions.
	float right = ImGui::GetWindowWidth() - 16;
	auto place = [&](float w) {
		right -= w;
		ImGui::SetCursorPos(ImVec2(right, 10));
		right -= 8;
	};

	place(92);
	ImGui::BeginDisabled(!a.server.Running());
	if (ImGui::Button("Open", ImVec2(92, 34)))
		util::OpenURL("http://localhost:" + std::to_string(a.server_port) + "/");
	ImGui::EndDisabled();
	ImGui::SetItemTooltip("open the local preview in a browser");

	place(112);
	if (a.server.Running()) {
		ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18f, 0.36f, 0.24f, 1.0f));
		if (ImGui::Button("Stop server", ImVec2(112, 34))) a.StartServer();
		ImGui::PopStyleColor();
		ImGui::SetItemTooltip("hugo server is running on port %d", a.server_port);
	} else {
		if (ImGui::Button("Preview site", ImVec2(112, 34))) a.StartServer();
		ImGui::SetItemTooltip("start `hugo server -D` and serve the site locally");
	}

	if (a.ed.dirty || a.cfg_dirty) {
		place(96);
		ImGui::PushStyleColor(ImGuiCol_Button, Theme::V4(Theme::ACCENT_DIM));
		if (ImGui::Button("Save", ImVec2(96, 34))) {
			if (a.ed.dirty) a.SavePost();
			if (a.cfg_dirty) a.SaveConfig();
		}
		ImGui::PopStyleColor();
		ImGui::SetItemTooltip("Ctrl+S");
	}

	ImGui::EndChild();
	ImGui::PopStyleColor();
}

void DrawStatusBar(App &a) {
	const float h = 26.0f;
	ImGui::SetCursorPosY(ImGui::GetWindowHeight() - h);
	ImGui::PushStyleColor(ImGuiCol_ChildBg, Theme::V4(Theme::RAIL));
	ImGui::BeginChild("statusbar", ImVec2(0, h), ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar);
	ImGui::SetCursorPos(ImVec2(14, 4));
	ImGui::PushFont(Theme::F_UI, Theme::UI_PX * 0.8f);

	ImU32 dot = a.server.Running() ? Theme::OK : Theme::TEXT_FAINT;
	ImVec2 p = ImGui::GetCursorScreenPos();
	ImGui::GetWindowDrawList()->AddCircleFilled(ImVec2(p.x + 4, p.y + 8), 4, dot, 12);
	ImGui::Dummy(ImVec2(14, 0));
	ImGui::SameLine();

	ImGui::PushStyleColor(ImGuiCol_Text, Theme::V4(Theme::TEXT_FAINT));
	ImGui::Text("%s   ·   %d page%s in content/   ·   %zu file%s in static+assets", a.site.root.c_str(),
	            (int)a.site.posts.size(), a.site.posts.size() == 1 ? "" : "s", a.site.media.size(),
	            a.site.media.size() == 1 ? "" : "s");
	if (!a.have_hugo) {
		ImGui::SameLine();
		ImGui::PushStyleColor(ImGuiCol_Text, Theme::V4(Theme::WARN));
		ImGui::TextUnformatted("   ·   hugo not on PATH: preview and build are unavailable");
		ImGui::PopStyleColor();
	}
	ImGui::PopStyleColor();

	if (!a.toast.empty() && ImGui::GetTime() < a.toast_until) {
		std::string t = a.toast;
		ImGui::PushFont(Theme::F_UI, Theme::UI_PX * 0.82f);
		float w = ImGui::CalcTextSize(t.c_str()).x;
		ImGui::SameLine(ImGui::GetWindowWidth() - w - 18);
		ImGui::PushStyleColor(ImGuiCol_Text, Theme::V4(a.toast_bad ? Theme::DANGER : Theme::OK));
		ImGui::TextUnformatted(t.c_str());
		ImGui::PopStyleColor();
		ImGui::PopFont();
	}
	ImGui::PopFont();
	ImGui::EndChild();
	ImGui::PopStyleColor();
}

void DrawModals(App &a) {
	if (a.want_new_post) {
		ImGui::OpenPopup("New post");
		a.want_new_post = false;
	}
	DrawNewPostModal(a);

	if (!a.confirm_delete.empty()) {
		ImGui::OpenPopup("Delete page");
	}
	if (ImGui::BeginPopupModal("Delete page", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
		ImGui::TextWrapped("Delete %s?", util::BaseName(a.confirm_delete).c_str());
		ImGui::PushStyleColor(ImGuiCol_Text, Theme::V4(Theme::TEXT_DIM));
		ImGui::TextWrapped("%s", a.confirm_delete.c_str());
		ImGui::TextWrapped("This removes the file from disk. If it is committed, git still has it.");
		ImGui::PopStyleColor();
		ImGui::Dummy(ImVec2(0, 6));
		if (W::DangerButton("Delete", ImVec2(110, 0))) {
			std::string err;
			std::string path = a.confirm_delete;
			if (site::DeletePost(path, &err)) {
				if (a.ed.path == path) a.CloseEditor();
				a.ReloadAll();
				a.Notify("deleted " + util::BaseName(path));
			} else {
				a.Notify(err, true);
			}
			a.confirm_delete.clear();
			ImGui::CloseCurrentPopup();
		}
		ImGui::SameLine();
		if (ImGui::Button("Cancel", ImVec2(110, 0))) {
			a.confirm_delete.clear();
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndPopup();
	}

	if (a.confirm_quit) {
		ImGui::OpenPopup("Unsaved changes");
		a.confirm_quit = false;
	}
	if (ImGui::BeginPopupModal("Unsaved changes", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
		ImGui::TextUnformatted("There are unsaved changes.");
		ImGui::PushStyleColor(ImGuiCol_Text, Theme::V4(Theme::TEXT_DIM));
		if (a.ed.dirty) ImGui::Text("  %s", util::BaseName(a.ed.path).c_str());
		if (a.cfg_dirty) ImGui::TextUnformatted("  hugo.toml");
		ImGui::PopStyleColor();
		ImGui::Dummy(ImVec2(0, 6));
		if (W::PrimaryButton("Save and quit", ImVec2(130, 0))) {
			if (a.ed.dirty) a.SavePost();
			if (a.cfg_dirty) a.SaveConfig();
			a.want_quit = true;
			ImGui::CloseCurrentPopup();
		}
		ImGui::SameLine();
		if (W::DangerButton("Discard", ImVec2(110, 0))) {
			a.want_quit = true;
			ImGui::CloseCurrentPopup();
		}
		ImGui::SameLine();
		if (ImGui::Button("Cancel", ImVec2(110, 0))) ImGui::CloseCurrentPopup();
		ImGui::EndPopup();
	}
}

void DrawSetupScreen(App &a) {
	ImGui::SetCursorPos(ImVec2(60, 120));
	ImGui::PushFont(Theme::F_BOLD, Theme::UI_PX * 1.6f);
	ImGui::TextUnformatted("No Hugo site here");
	ImGui::PopFont();
	ImGui::Dummy(ImVec2(0, 10));
	ImGui::SetCursorPosX(60);
	ImGui::PushStyleColor(ImGuiCol_Text, Theme::V4(Theme::TEXT_DIM));
	ImGui::PushTextWrapPos(760);
	ImGui::TextUnformatted(a.site.error.c_str());
	ImGui::TextUnformatted(
	    "\nhugofe looks for a hugo.toml (or config.toml) next to a content/ directory, walking up from "
	    "the directory it was started in. Pass the site directory as the first argument to point it "
	    "somewhere else:\n\n    ./hugofe ~/Claude/hugofe");
	ImGui::PopTextWrapPos();
	ImGui::PopStyleColor();
}

void HandleShortcuts(App &a) {
	ImGuiIO &io = ImGui::GetIO();
	if (!io.KeyCtrl) return;
	if (ImGui::IsKeyPressed(ImGuiKey_S, false)) {
		if (a.ed.dirty) a.SavePost();
		if (a.cfg_dirty) a.SaveConfig();
		if (!a.ed.dirty && !a.cfg_dirty) a.Notify("nothing to save");
	}
	// Ctrl+N is only "new post" when a text field isn't capturing keys.
	if (!io.WantTextInput && ImGui::IsKeyPressed(ImGuiKey_N, false)) a.want_new_post = true;
	if (!io.WantTextInput && ImGui::IsKeyPressed(ImGuiKey_R, false)) {
		a.ReloadAll();
		a.Notify("rescanned the site");
	}
}

void HandleDroppedFiles(App &a) {
	if (a.dropped_files.empty()) return;
	// Dropped files land in static/img/ -- the place a post can reference
	// with a plain /img/<name> URL, no Hugo Pipes needed.
	std::string dest_dir = a.site.root + "/static/img";
	util::MakeDirs(dest_dir);
	int n = 0;
	std::string last_web;
	for (const std::string &src : a.dropped_files) {
		std::string data;
		if (!util::ReadFile(src, &data)) continue;
		std::string name = util::BaseName(src);
		std::string dest = dest_dir + "/" + name;
		for (int i = 2; util::Exists(dest); i++)
			dest = dest_dir + "/" + util::StripExt(name) + "-" + std::to_string(i) +
			       name.substr(util::StripExt(name).size());
		if (!util::WriteFileAtomic(dest, data)) continue;
		last_web = "/img/" + util::BaseName(dest);
		n++;
	}
	a.dropped_files.clear();
	if (n == 0) {
		a.Notify("could not copy the dropped file", true);
		return;
	}
	a.site.ScanMedia();
	if (!a.ed.path.empty() && a.tab == Tab::Editor) {
		// Dropping onto an open post inserts the reference where the caret is.
		int sa = a.ed.sel_a, sb = a.ed.sel_b;
		md::InsertText(&a.ed.body, &sa, &sb, "![](" + last_web + ")\n");
		a.ed.sel_a = sa;
		a.ed.sel_b = sb;
		a.ed.dirty = true;
		a.Notify("copied to static/img and inserted the reference");
	} else {
		a.Notify(std::to_string(n) + " file(s) copied into static/img");
	}
}

} // namespace


// ---------------------------------------------------------------- selftest
//
// The toolbar's edit path is the one thing in this program that cannot be
// tested without a real ImGui frame loop: the text is changed from inside an
// InputText callback specifically because ImGui re-applies a widget's own
// buffer on the frame after it loses focus. A unit test on md::WrapSelection
// proves the transform; only this proves the plumbing around it. It runs the
// actual window, actual widget and actual callback, and checks the buffer
// afterwards.
namespace {

struct SelfTest {
	int frame = 0;
	int failures = 0;
	int checks = 0;
	std::string path;

	void Check(bool ok, const std::string &what, const std::string &got = "") {
		checks++;
		if (ok) {
			printf("  ok    %s\n", what.c_str());
		} else {
			failures++;
			printf("  FAIL  %s%s%s\n", what.c_str(), got.empty() ? "" : "  got: ", got.c_str());
		}
	}

	// Returns false when the run is over.
	bool Step(App &a) {
		frame++;
		switch (frame) {
		case 3: {
			const char *tmp = getenv("TMPDIR");
			path = std::string(tmp && *tmp ? tmp : "/tmp") + "/hugofe-selftest-" +
			       std::to_string((int)getpid()) + ".md";
			util::WriteFileAtomic(path,
			                      "+++\ntitle = 'Self test'\ndraft = true\ntags = ['a']\n+++\n"
			                      "hello world\nsecond line\n");
			printf("== editor, driven through real frames\n");
			Check(a.OpenPost(path), "opens a post");
			Check(a.ed.body == "hello world\nsecond line\n", "body parsed out of the front matter", a.ed.body);
			Check(a.ed.doc.GetStr("title") == "Self test", "title parsed", a.ed.doc.GetStr("title"));
			a.tab = Tab::Editor;
			break;
		}
		case 5:
			// Select "hello" and ask for bold, exactly as the B button does.
			a.ed.sel_a = 0;
			a.ed.sel_b = 5;
			a.ed.pending = EditAction::Bold;
			a.ed.focus_editor = true;
			break;
		case 9:
			Check(a.ed.body == "**hello** world\nsecond line\n", "B wraps the selection", a.ed.body);
			Check(a.ed.sel_a == 2 && a.ed.sel_b == 7, "the selection follows the markers",
			      std::to_string(a.ed.sel_a) + ".." + std::to_string(a.ed.sel_b));
			Check(a.ed.dirty, "the edit marks the post dirty");
			// Same selection again: it must toggle back off, not nest.
			a.ed.pending = EditAction::Bold;
			a.ed.focus_editor = true;
			break;
		case 13:
			Check(a.ed.body == "hello world\nsecond line\n", "B again toggles it back off", a.ed.body);
			a.ed.sel_a = a.ed.sel_b = 13;  // somewhere on line two
			a.ed.pending = EditAction::H2;
			a.ed.focus_editor = true;
			break;
		case 17:
			Check(a.ed.body == "hello world\n## second line\n", "H2 applies to the caret's line", a.ed.body);
			a.ed.sel_a = 0;
			a.ed.sel_b = 11;
			a.ed.pending = EditAction::Bullet;
			a.ed.focus_editor = true;
			break;
		case 21:
			Check(a.ed.body == "- hello world\n## second line\n", "list prefixes the selected line",
			      a.ed.body);
			Check(a.SavePost(), "saves");
			break;
		case 23: {
			std::string on_disk;
			util::ReadFile(path, &on_disk);
			Check(on_disk.find("- hello world") != std::string::npos, "the body reached the file", on_disk);
			Check(on_disk.find("title = \"Self test\"") != std::string::npos, "the front matter survived",
			      on_disk);
			Check(on_disk.find("tags = [\"a\"]") != std::string::npos, "an untouched key survived", on_disk);
			Check(!a.ed.dirty, "saving clears the dirty flag");
			printf("== hugo.toml, edited in place\n");
			std::string before = a.cfg.Text();
			a.cfg.SetString("params.profileMode.subtitle", "driven by the selftest");
			Check(a.cfg.GetString("params.profileMode.subtitle") == "driven by the selftest",
			      "a design edit reads back");
			Check(a.cfg.Text() != before, "the document changed");
			Check(a.cfg.Text().find("# Enables PaperMod's Fuse.js") != std::string::npos ||
			          before.find("# Enables PaperMod's Fuse.js") == std::string::npos,
			      "comments in hugo.toml survived the edit");
			// Put it back: a selftest must not leave the repo modified.
			a.cfg.SetText(before);
			a.cfg_dirty = false;
			unlink(path.c_str());
			break;
		}
		case 25:
			printf("\n%d checks, %d failed\n", checks, failures);
			return false;
		}
		return true;
	}
};

} // namespace

// --------------------------------------------------------------------- main

int main(int argc, char **argv) {
	std::string site_dir;
	std::string screenshot_dir;
	bool selftest = false;
	for (int i = 1; i < argc; i++) {
		std::string arg = argv[i];
		if (arg == "--screenshot" && i + 1 < argc) screenshot_dir = argv[++i];
		else if (arg == "--selftest") selftest = true;
		else if (arg == "-h" || arg == "--help") {
			printf("hugofe -- an ImGui front end for a Hugo site\n\n"
			       "  hugofe [SITE_DIR] [--screenshot DIR]\n\n"
			       "  SITE_DIR       the site to manage; default: walk up from $PWD\n"
			       "  --screenshot   render every tab to PNGs in DIR and exit\n");
			return 0;
		} else if (!arg.empty() && arg[0] != '-') site_dir = arg;
	}

	EnsureToolPath();
	App app;
	char cwd[4096];
	if (site_dir.empty() && getcwd(cwd, sizeof cwd)) site_dir = cwd;
	bool have_site = app.site.Detect(site_dir);
	if (have_site) {
		app.site.Scan();
		app.site.ScanMedia();
		app.cfg.Load(app.site.config);
		util::ReadFile(app.site.config, &app.cfg_text_on_disk);
	}
	app.have_hugo = HaveTool("hugo");
	app.have_git = HaveTool("git");
	app.have_gh = HaveTool("gh");

	if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) {
		fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
		return 1;
	}
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, 0);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 2);
	SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
	SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);

	SDL_Window *window = SDL_CreateWindow("hugofe", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1560, 980,
	                                      SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
	if (!window) {
		fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError());
		return 1;
	}
	SDL_GLContext gl = SDL_GL_CreateContext(window);
	SDL_GL_MakeCurrent(window, gl);
	SDL_GL_SetSwapInterval(1);
	SDL_EventState(SDL_DROPFILE, SDL_ENABLE);

	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO &io = ImGui::GetIO();
	io.IniFilename = nullptr; // no imgui.ini: the layout is fixed, not dockable
	io.ConfigInputTextCursorBlink = true;
	Theme::LoadFonts(io, 17.0f);
	Theme::Apply();
	ImGui_ImplSDL2_InitForOpenGL(window, gl);
	ImGui_ImplOpenGL3_Init("#version 130");

	int shot_frame = 0;
	SelfTest st;
	bool running = true;
	while (running) {
		SDL_Event e;
		while (SDL_PollEvent(&e)) {
			ImGui_ImplSDL2_ProcessEvent(&e);
			if (e.type == SDL_QUIT) app.want_quit = true;
			if (e.type == SDL_WINDOWEVENT && e.window.event == SDL_WINDOWEVENT_CLOSE &&
			    e.window.windowID == SDL_GetWindowID(window))
				app.want_quit = true;
			if (e.type == SDL_DROPFILE && e.drop.file) {
				app.dropped_files.push_back(e.drop.file);
				SDL_free(e.drop.file);
			}
		}
		if (app.want_quit) {
			if (app.ed.dirty || app.cfg_dirty) {
				app.want_quit = false;
				app.confirm_quit = true;
			} else {
				running = false;
			}
		}

		app.server.Poll();
		app.task.Poll();

		ImGui_ImplOpenGL3_NewFrame();
		ImGui_ImplSDL2_NewFrame();
		ImGui::NewFrame();

		const ImGuiViewport *vp = ImGui::GetMainViewport();
		ImGui::SetNextWindowPos(vp->WorkPos);
		ImGui::SetNextWindowSize(vp->WorkSize);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
		ImGui::Begin("##root", nullptr,
		             ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
		                 ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus);
		ImGui::PopStyleVar();

		if (!have_site) {
			DrawSetupScreen(app);
		} else {
			HandleShortcuts(app);
			HandleDroppedFiles(app);
			DrawTopBar(app);
			ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(14, 12));
			// AlwaysUseWindowPadding: a borderless child otherwise gets its
			// padding zeroed by ImGui, which left every tab's content flush
			// against the left edge of the window.
			ImGui::BeginChild("body", ImVec2(0, ImGui::GetContentRegionAvail().y - 26),
			                  ImGuiChildFlags_AlwaysUseWindowPadding);
			switch (app.tab) {
			case Tab::Content: DrawContentTab(app); break;
			case Tab::Editor: DrawEditorTab(app); break;
			case Tab::Design: DrawDesignTab(app); break;
			case Tab::Media: DrawMediaTab(app); break;
			case Tab::Publish: DrawPublishTab(app); break;
			default: break;
			}
			ImGui::EndChild();
			ImGui::PopStyleVar();
			DrawStatusBar(app);
			DrawModals(app);
		}
		ImGui::End();

		ImGui::Render();
		int w, h;
		SDL_GL_GetDrawableSize(window, &w, &h);
		ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
		SDL_GL_SwapWindow(window);

		if (selftest && !st.Step(app)) running = false;

		if (!screenshot_dir.empty()) {
			// One tab per capture, a couple of warm-up frames each so fonts
			// are baked and any first-frame layout has settled.
			struct Step { int frame; Tab tab; const char *name; };
			static const Step steps[] = {
			    {6, Tab::Content, "1-content"}, {12, Tab::Editor, "2-editor"},
			    {18, Tab::Design, "3-design"},  {24, Tab::Media, "4-media"},
			    {30, Tab::Publish, "5-publish"},
			    {36, Tab::Design, "6-design-postlist"},
			};
			shot_frame++;
			for (const Step &s : steps) {
				if (shot_frame == s.frame - 3) {
					app.tab = s.tab;
					// The second Design capture is the post-list mock.
					if (s.frame == 36) app.design_page = 1;
					if (s.tab == Tab::Editor && app.ed.path.empty() && !app.site.posts.empty()) {
						// Open the longest page, so the preview has something
						// worth looking at.
						size_t best = 0;
						for (size_t i = 1; i < app.site.posts.size(); i++)
							if (app.site.posts[i].size > app.site.posts[best].size) best = i;
						app.OpenPost(app.site.posts[best].path);
						app.tab = s.tab;
					}
				}
				if (shot_frame == s.frame) {
					std::string p = screenshot_dir + "/" + s.name + ".png";
					if (shot::Capture(w, h, p)) printf("wrote %s (%dx%d)\n", p.c_str(), w, h);
					else fprintf(stderr, "failed to write %s\n", p.c_str());
				}
			}
			if (shot_frame > steps[5].frame) running = false;
		}
	}

	if (app.server.Running()) app.server.Stop();
	if (app.task.Running()) app.task.Stop();
	ImGui_ImplOpenGL3_Shutdown();
	ImGui_ImplSDL2_Shutdown();
	ImGui::DestroyContext();
	SDL_GL_DeleteContext(gl);
	SDL_DestroyWindow(window);
	SDL_Quit();
	return selftest ? (st.failures ? 1 : 0) : 0;
}
