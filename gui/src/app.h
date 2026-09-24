// app -- the shared state every tab draws from, and the tab entry points.
//
// One App instance lives on main()'s stack for the life of the process.
// Everything that touches the site on disk goes through it, so there is
// exactly one place that knows whether the open post is dirty, which
// subprocess is running, and when the content tree was last rescanned.
#pragma once

#include "fm.h"
#include "md.h"
#include "md_render.h"
#include "proc.h"
#include "site.h"
#include "theme.h"
#include "toml_edit.h"

#include <string>
#include <vector>

enum class Tab { Content, Editor, Design, Media, Publish, COUNT };

// A toolbar action, applied to the editor's text on the next frame from
// inside the InputText callback. It has to happen there rather than here:
// ImGui re-applies a widget's own stored text on the frame after it loses
// focus, so an edit made straight from a button handler gets silently
// reverted (imgui_widgets.cpp, "Handle reapplying final data on
// deactivation"). Going through the callback keeps ImGui's copy and ours the
// same string at all times.
enum class EditAction { None, Bold, Italic, InlineCode, Strike, H1, H2, H3, Bullet, Number, Quote, CodeBlock, Link, Image, Rule, Table };

struct Editor {
	std::string path;              // open file, empty = nothing open
	fm::Doc doc;                   // front matter; `body` below is the live text
	std::string body;
	std::string saved_snapshot;    // what is on disk, for the dirty check
	bool dirty = false;

	int view = 0;                  // 0 = split, 1 = source, 2 = preview
	bool preview_dark = true;      // matches defaultTheme by default

	// Selection tracking, refreshed from the InputText callback every frame
	// the widget is active. The toolbar needs it after focus has moved away.
	int sel_a = 0, sel_b = 0;
	EditAction pending = EditAction::None;
	std::string pending_arg;       // the URL, for Link/Image
	bool focus_editor = false;

	std::vector<md::Block> blocks; // reparsed only when the body changes
	std::string blocks_of;

	// Height the front-matter bar needs, measured from the previous frame's
	// content so a long tag list gets a taller bar instead of a clipped one.
	float fm_height = 0;

	std::string tag_input;
	std::string new_key;
	std::string link_url;
	std::string status;
};

struct App {
	site::Site site;
	tomledit::Doc cfg;
	bool cfg_dirty = false;
	std::string cfg_text_on_disk;

	Editor ed;
	Tab tab = Tab::Content;

	// Content tab
	std::string filter;
	int selected = -1;             // index into site.posts
	bool show_drafts = true;
	int section_filter = 0;        // 0 = all

	// Design tab
	int design_page = 0;           // which mock to show: home / post list
	bool design_dark = true;
	int menu_selected = -1;
	int social_selected = -1;
	int button_selected = -1;

	// Media tab
	std::string media_filter;
	int media_selected = -1;
	std::vector<std::string> dropped_files;  // from SDL_DROPFILE

	// Publish tab
	std::string commit_msg;
	std::string git_status;
	std::string gh_runs;
	double git_status_at = 0;

	Proc server;                   // hugo server -D
	Proc task;                     // build / git / gh, one at a time
	int server_port = 1313;
	bool have_hugo = false, have_git = false, have_gh = false;

	std::string toast;
	double toast_until = 0;
	bool toast_bad = false;

	// Modal requests, raised from anywhere and drawn by main().
	bool want_new_post = false;
	std::string confirm_delete;    // path pending a delete confirmation
	bool want_quit = false;
	bool confirm_quit = false;

	void Notify(const std::string &msg, bool bad = false);
	void ReloadAll();
	void ReloadConfig();
	bool OpenPost(const std::string &path);
	bool SavePost();
	bool SaveConfig();
	void CloseEditor();
	// Reparses the body into blocks, but only when it has actually changed.
	const std::vector<md::Block> &Blocks();
	md::RenderStyle PreviewStyle(bool dark) const;
	std::string BaseURL() const;
	// The URL a post is served at by `hugo server`, drafts included.
	std::string LocalURL(const site::Post &p) const;
	void StartServer();
	void RunTask(const std::vector<std::string> &argv);
	std::vector<std::string> AllTags() const;
};

void DrawContentTab(App &a);
void DrawEditorTab(App &a);
void DrawDesignTab(App &a);
void DrawMediaTab(App &a);
void DrawPublishTab(App &a);
void DrawNewPostModal(App &a);
