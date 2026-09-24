#include "site.h"

#include "util.h"

#include <algorithm>
#include <cstdio>
#include <dirent.h>
#include <unistd.h>

namespace site {
namespace {

void WalkMarkdown(const std::string &dir, const std::string &prefix, std::vector<std::string> *out) {
	DIR *d = opendir(dir.c_str());
	if (!d) return;
	struct dirent *e;
	while ((e = readdir(d))) {
		std::string name = e->d_name;
		if (name == "." || name == "..") continue;
		std::string full = dir + "/" + name;
		std::string rel = prefix.empty() ? name : prefix + "/" + name;
		if (util::IsDir(full)) WalkMarkdown(full, rel, out);
		else if (util::EndsWith(util::Lower(name), ".md") || util::EndsWith(util::Lower(name), ".markdown"))
			out->push_back(rel);
	}
	closedir(d);
}

void WalkAll(const std::string &dir, const std::string &prefix, std::vector<std::string> *out) {
	DIR *d = opendir(dir.c_str());
	if (!d) return;
	struct dirent *e;
	while ((e = readdir(d))) {
		std::string name = e->d_name;
		if (name == "." || name == ".." || name[0] == '.') continue;
		std::string full = dir + "/" + name;
		std::string rel = prefix.empty() ? name : prefix + "/" + name;
		if (util::IsDir(full)) WalkAll(full, rel, out);
		else out->push_back(rel);
	}
	closedir(d);
}

bool IsImageExt(const std::string &p) {
	std::string l = util::Lower(p);
	return util::EndsWith(l, ".png") || util::EndsWith(l, ".jpg") || util::EndsWith(l, ".jpeg") ||
	       util::EndsWith(l, ".gif") || util::EndsWith(l, ".webp") || util::EndsWith(l, ".svg") ||
	       util::EndsWith(l, ".ico") || util::EndsWith(l, ".avif");
}

} // namespace

std::string Permalink(const std::string &rel, const std::string &url_override) {
	std::string u = util::Trim(url_override);
	if (!u.empty()) {
		if (u[0] != '/') u = "/" + u;
		if (u.back() != '/') u += "/";
		return u;
	}
	std::string p = util::StripExt(rel);
	std::string base = util::BaseName(p);
	if (base == "_index" || base == "index") {
		std::string dir = util::DirName(p);
		p = (dir == "." || dir.empty()) ? "" : dir;
	}
	std::string out = "/" + p;
	if (out.size() > 1 && out.back() != '/') out += "/";
	if (out.empty()) out = "/";
	return out;
}

bool Site::Detect(const std::string &start) {
	std::string dir = start;
	char buf[4096];
	if (dir.empty() || dir[0] != '/') {
		if (getcwd(buf, sizeof buf)) dir = std::string(buf) + (dir.empty() || dir == "." ? "" : "/" + dir);
	}
	for (int depth = 0; depth < 8 && !dir.empty() && dir != "/"; depth++) {
		for (const char *name : {"hugo.toml", "config.toml", "hugo.yaml", "config.yaml"}) {
			std::string c = dir + "/" + name;
			if (util::Exists(c) && util::IsDir(dir + "/content")) {
				root = dir;
				config = c;
				return true;
			}
		}
		dir = util::DirName(dir);
	}
	error = "no hugo.toml with a content/ directory found above " + start;
	return false;
}

void Site::Scan() {
	posts.clear();
	sections.clear();
	std::string cdir = root + "/content";
	std::vector<std::string> rels;
	WalkMarkdown(cdir, "", &rels);
	std::sort(rels.begin(), rels.end());

	for (const std::string &rel : rels) {
		Post p;
		p.rel = rel;
		p.path = cdir + "/" + rel;
		p.mtime = util::MTime(p.path);
		p.size = util::FileSize(p.path);
		size_t slash = rel.find('/');
		p.section = (slash == std::string::npos) ? "" : rel.substr(0, slash);
		p.is_branch = util::BaseName(rel) == "_index.md";

		std::string text;
		if (util::ReadFile(p.path, &text)) {
			fm::Doc d = fm::Parse(text);
			p.title = d.GetStr("title");
			p.date = d.GetStr("date");
			if (p.date.empty()) p.date = d.GetStr("publishDate");
			p.summary = d.GetStr("summary");
			p.layout = d.GetStr("layout");
			p.draft = d.GetBool("draft", false);
			p.tags = d.GetList("tags");
			if (p.tags.empty()) p.tags = d.GetList("categories");
			p.words = fm::WordCount(d.body);
			p.permalink = Permalink(rel, d.GetStr("url"));
		}
		if (p.title.empty()) p.title = util::BaseName(util::StripExt(rel));
		posts.push_back(p);

		if (!p.section.empty() && std::find(sections.begin(), sections.end(), p.section) == sections.end())
			sections.push_back(p.section);
	}
	std::sort(sections.begin(), sections.end());
}

void Site::ScanMedia() {
	media.clear();
	for (const char *base : {"static", "assets"}) {
		std::string dir = root + "/" + base;
		if (!util::IsDir(dir)) continue;
		std::vector<std::string> rels;
		WalkAll(dir, "", &rels);
		std::sort(rels.begin(), rels.end());
		for (const std::string &rel : rels) {
			MediaFile m;
			m.rel = std::string(base) + "/" + rel;
			m.path = dir + "/" + rel;
			// static/ is published at the site root; assets/ goes through
			// Hugo Pipes and has no direct URL, so show the resource path.
			m.web = (std::string(base) == "static") ? "/" + rel : rel;
			m.size = util::FileSize(m.path);
			m.image = IsImageExt(rel);
			media.push_back(m);
		}
	}
}

Post *Site::Find(const std::string &path) {
	for (auto &p : posts)
		if (p.path == path) return &p;
	return nullptr;
}

std::string RenderArchetype(const Site &s, const std::string &section, const std::string &title,
                            const std::string &slug) {
	std::string tpl;
	bool have = false;
	if (!section.empty()) have = util::ReadFile(s.root + "/archetypes/" + section + ".md", &tpl);
	if (!have) have = util::ReadFile(s.root + "/archetypes/default.md", &tpl);
	if (!have)
		tpl =
		    "+++\n"
		    "date = '{{ .Date }}'\n"
		    "draft = true\n"
		    "title = '{{ replace .File.ContentBaseName \"-\" \" \" | title }}'\n"
		    "+++\n";

	std::string shown = title.empty() ? util::Replace(slug, "-", " ") : title;
	// Hugo's archetype quotes the title with '...' -- a title containing an
	// apostrophe would produce invalid TOML, so it goes out double-quoted and
	// escaped instead. Both quoting styles parse the same on the way back in.
	std::string quoted_ok = shown;
	bool needs_dq = shown.find('\'') != std::string::npos;

	std::string out;
	for (size_t i = 0; i < tpl.size();) {
		if (tpl.compare(i, 2, "{{") == 0) {
			size_t end = tpl.find("}}", i);
			if (end != std::string::npos) {
				std::string expr = util::Trim(tpl.substr(i + 2, end - i - 2));
				std::string value;
				bool handled = false;
				if (expr == ".Date") {
					value = util::NowRFC3339();
					handled = true;
				} else if (expr.find(".File.ContentBaseName") != std::string::npos) {
					value = quoted_ok;
					handled = true;
					if (needs_dq) {
						// Swap the surrounding single quotes for double ones.
						size_t q = out.find_last_of('\'');
						if (q != std::string::npos && q + 1 == out.size()) {
							out[q] = '"';
							value = util::Replace(value, "\"", "\\\"");
						}
					}
				}
				if (handled) {
					out += value;
					i = end + 2;
					if (needs_dq && expr.find(".File.ContentBaseName") != std::string::npos &&
					    i < tpl.size() && tpl[i] == '\'') {
						out += '"';
						i++;
					}
					continue;
				}
				// An expression this doesn't understand is copied through so
				// the author can see (and fix) it rather than losing it.
				out += tpl.substr(i, end + 2 - i);
				i = end + 2;
				continue;
			}
		}
		out += tpl[i++];
	}
	return out;
}

bool NewPost(const Site &s, const std::string &section, const std::string &title, const std::string &slug,
             std::string *out_path, std::string *err) {
	if (slug.empty()) { *err = "the file name is empty -- give the post a title"; return false; }
	std::string dir = s.root + "/content" + (section.empty() ? "" : "/" + section);
	if (!util::MakeDirs(dir)) { *err = "could not create " + dir; return false; }
	std::string path = dir + "/" + slug + ".md";
	if (util::Exists(path)) { *err = slug + ".md already exists in that section"; return false; }
	std::string body = RenderArchetype(s, section, title, slug);
	if (!util::WriteFileAtomic(path, body)) { *err = "could not write " + path; return false; }
	*out_path = path;
	return true;
}

bool DeletePost(const std::string &path, std::string *err) {
	if (unlink(path.c_str()) != 0) { *err = "could not delete " + path; return false; }
	return true;
}

bool RenamePost(const std::string &path, const std::string &new_path, std::string *err) {
	if (util::Exists(new_path)) { *err = util::BaseName(new_path) + " already exists"; return false; }
	if (!util::MakeDirs(util::DirName(new_path))) { *err = "could not create the target directory"; return false; }
	if (rename(path.c_str(), new_path.c_str()) != 0) { *err = "could not rename to " + new_path; return false; }
	return true;
}

} // namespace site
