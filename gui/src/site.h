// site -- the Hugo site model: where the site is, what's in content/, and
// the file operations the Content tab offers.
//
// No ImGui here either; the whole thing is filesystem + fm, so the tests can
// exercise it directly.
#pragma once

#include "fm.h"

#include <string>
#include <vector>

namespace site {

struct Post {
	std::string path;      // absolute
	std::string rel;       // relative to content/, e.g. "posts/hello.md"
	std::string section;   // "posts", or "" for a root-level page
	std::string title;
	std::string date;      // as written in front matter
	std::string summary;
	std::string layout;
	std::string permalink; // "/posts/hello/"
	bool draft = false;
	bool is_branch = false; // _index.md -- a section's own page
	std::vector<std::string> tags;
	int words = 0;
	long long mtime = 0;
	long long size = 0;
};

struct MediaFile {
	std::string path;  // absolute
	std::string rel;   // relative to the site root, e.g. "static/img/a.png"
	std::string web;   // how a post references it, e.g. "/img/a.png"
	long long size = 0;
	bool image = false;
};

struct Site {
	std::string root;    // directory holding hugo.toml
	std::string config;  // <root>/hugo.toml (or config.toml)
	std::vector<Post> posts;
	std::vector<std::string> sections;
	std::vector<MediaFile> media;
	std::string error;

	// Walks up from `start` looking for hugo.toml/config.toml, so the binary
	// works from gui/ as well as from the site root.
	bool Detect(const std::string &start);
	void Scan();      // content/**.md -> posts, sections
	void ScanMedia(); // static/** and assets/** -> media
	Post *Find(const std::string &path);
};

// content-relative path -> the URL Hugo will publish it at. `url_override` is
// front matter's own `url`, which wins when set.
std::string Permalink(const std::string &rel, const std::string &url_override);

// Renders archetypes/<section>.md (falling back to archetypes/default.md, and
// then to a built-in) for a new post. Only the two template expressions Hugo's
// own default archetype uses are supported -- `{{ .Date }}` and the
// ContentBaseName title -- which is exactly what this site's archetype has.
// Anything else in a custom archetype is left verbatim rather than guessed at.
std::string RenderArchetype(const Site &s, const std::string &section, const std::string &title,
                            const std::string &slug);

// Creates content/<section>/<slug>.md. Fails rather than overwrite.
bool NewPost(const Site &s, const std::string &section, const std::string &title, const std::string &slug,
             std::string *out_path, std::string *err);

bool DeletePost(const std::string &path, std::string *err);
bool RenamePost(const std::string &path, const std::string &new_path, std::string *err);

} // namespace site
