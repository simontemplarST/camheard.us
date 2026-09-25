#include "audit.h"

#include "fm.h"
#include "util.h"

#include <algorithm>
#include <cctype>
#include <cstring>

namespace audit {
namespace {

// Inline code spans are stripped before a line is scanned, so `[a](b)` in a
// sentence about markdown doesn't come back as a broken link. Backticks are
// replaced by spaces rather than deleted so nothing else shifts.
std::string BlankCodeSpans(const std::string &line) {
	std::string out = line;
	size_t i = 0;
	while (i < out.size()) {
		if (out[i] == '`') {
			size_t j = out.find('`', i + 1);
			if (j == std::string::npos) break;
			for (size_t k = i; k <= j; k++) out[k] = ' ';
			i = j + 1;
		} else i++;
	}
	return out;
}

// The target inside a (...) destination: "<url>" unwrapped, a "title"
// dropped, surrounding space gone.
std::string CleanTarget(const std::string &raw) {
	std::string t = util::Trim(raw);
	if (t.size() >= 2 && t.front() == '<' && t.back() == '>') t = t.substr(1, t.size() - 2);
	size_t sp = t.find_first_of(" \t");
	if (sp != std::string::npos) t = t.substr(0, sp);
	return util::Trim(t);
}

// src="..." / href="..." out of a raw HTML line. The site has unsafe = true,
// so hand-written <img> tags do end up in the output.
void ScanHtmlAttrs(const std::string &line, int line_no, std::vector<Ref> *out) {
	struct A { const char *name; bool image; };
	static const A attrs[] = {{"src=", true}, {"href=", false}};
	for (const A &at : attrs) {
		size_t i = 0;
		while ((i = line.find(at.name, i)) != std::string::npos) {
			size_t v = i + strlen(at.name);
			if (v >= line.size()) break;
			char q = line[v];
			std::string target;
			if (q == '"' || q == '\'') {
				size_t e = line.find(q, v + 1);
				if (e == std::string::npos) break;
				target = line.substr(v + 1, e - v - 1);
				i = e + 1;
			} else {
				size_t e = line.find_first_of(" \t>", v);
				target = line.substr(v, (e == std::string::npos ? line.size() : e) - v);
				i = v + target.size();
			}
			target = util::Trim(target);
			if (!target.empty()) out->push_back({target, at.image, line_no});
		}
	}
}

bool HasImageExt(const std::string &p) {
	std::string l = util::Lower(p);
	for (const char *e : {".png", ".jpg", ".jpeg", ".gif", ".webp", ".svg", ".avif", ".ico"})
		if (util::EndsWith(l, e)) return true;
	return false;
}

// "/posts/hello/?x=1#top" -> "/posts/hello/". A link with a file extension
// keeps it: /files/paper.pdf is a file, not a page.
std::string NormalisePagePath(std::string t) {
	size_t cut = t.find_first_of("#?");
	if (cut != std::string::npos) t = t.substr(0, cut);
	if (t.empty()) return "/";
	if (t.back() != '/' && util::BaseName(t).find('.') == std::string::npos) t += "/";
	return t;
}

// A relative link resolved against the page it was written on. Hugo resolves
// these against the page's own URL, which is what a browser does too.
std::string ResolveRelative(const std::string &base_permalink, const std::string &target) {
	if (!target.empty() && target[0] == '/') return target;
	std::string dir = base_permalink;
	if (dir.empty() || dir.back() != '/') dir = util::DirName(dir) + "/";
	std::string joined = dir + target;
	// Collapse ../ and ./ the way a URL does.
	std::vector<std::string> parts;
	std::string cur;
	for (size_t i = 0; i <= joined.size(); i++) {
		if (i == joined.size() || joined[i] == '/') {
			if (cur == "..") { if (!parts.empty()) parts.pop_back(); }
			else if (cur != "." && !cur.empty()) parts.push_back(cur);
			cur.clear();
		} else cur += joined[i];
	}
	std::string out = "/" + util::Join(parts, "/");
	if (!joined.empty() && joined.back() == '/' && out.back() != '/') out += "/";
	return out;
}

} // namespace

bool AlwaysShipped(const std::string &rel) {
	std::string base = util::Lower(util::BaseName(rel));
	static const char *names[] = {"cname",           "robots.txt",       "favicon.ico",
	                              "favicon.png",     "favicon-16x16.png", "favicon-32x32.png",
	                              "apple-touch-icon.png", "site.webmanifest", ".nojekyll",
	                              "browserconfig.xml", "safari-pinned-tab.svg", "manifest.json"};
	for (const char *n : names)
		if (base == n) return true;
	return false;
}

namespace {

void Add(Report *r, Kind k, Level lv, const std::string &path, const std::string &where,
         const std::string &subject, const std::string &detail, int line, bool media = false) {
	Finding f;
	f.kind = k;
	f.level = lv;
	f.path = path;
	f.where = where;
	f.subject = subject;
	f.detail = detail;
	f.line = line;
	f.is_media = media;
	r->findings.push_back(f);
	if (lv == Level::Error) r->errors++;
	else if (lv == Level::Warn) r->warnings++;
	else r->infos++;
}

} // namespace

bool IsExternal(const std::string &t) {
	if (t.empty()) return true;
	if (t[0] == '#') return true;
	if (t.find("{{") != std::string::npos) return true; // a template fills this in
	static const char *schemes[] = {"http://", "https://", "//", "mailto:", "tel:", "data:", "ftp://"};
	for (const char *s : schemes)
		if (util::StartsWith(t, s)) return true;
	return false;
}

std::vector<Ref> ExtractRefs(const std::string &body) {
	std::vector<Ref> out;
	std::vector<std::string> lines = util::SplitLines(body);
	bool in_fence = false;
	std::string fence;
	for (size_t ln = 0; ln < lines.size(); ln++) {
		std::string trimmed = util::Trim(lines[ln]);
		if (in_fence) {
			if (trimmed.compare(0, fence.size(), fence) == 0) in_fence = false;
			continue;
		}
		if (trimmed.compare(0, 3, "```") == 0 || trimmed.compare(0, 3, "~~~") == 0) {
			in_fence = true;
			fence = trimmed.substr(0, 3);
			continue;
		}
		std::string line = BlankCodeSpans(lines[ln]);
		int line_no = (int)ln + 1;

		// [text](target) and ![alt](target).
		for (size_t i = 0; i < line.size(); i++) {
			if (line[i] != '[') continue;
			bool image = (i > 0 && line[i - 1] == '!');
			int depth = 1;
			size_t j = i + 1;
			for (; j < line.size() && depth; j++) {
				if (line[j] == '\\') { j++; continue; }
				if (line[j] == '[') depth++;
				else if (line[j] == ']') depth--;
			}
			if (depth) break;              // unbalanced: nothing more to find here
			if (j >= line.size()) break;
			if (line[j] != '(') { i = j - 1; continue; }
			size_t close = line.find(')', j);
			if (close == std::string::npos) break;
			std::string t = CleanTarget(line.substr(j + 1, close - j - 1));
			if (!t.empty()) out.push_back({t, image, line_no});
			i = close;
		}

		// [id]: target -- a reference-style definition.
		if (util::StartsWith(trimmed, "[")) {
			size_t rb = trimmed.find("]:");
			if (rb != std::string::npos) {
				std::string t = CleanTarget(trimmed.substr(rb + 2));
				if (!t.empty()) out.push_back({t, HasImageExt(t), line_no});
			}
		}

		if (line.find('<') != std::string::npos) ScanHtmlAttrs(line, line_no, &out);
	}
	return out;
}

bool Report::Unreferenced(const std::string &media_rel) const {
	return std::find(unreferenced.begin(), unreferenced.end(), media_rel) != unreferenced.end();
}

int Report::Count(Kind k) const {
	int n = 0;
	for (const Finding &f : findings)
		if (f.kind == k) n++;
	return n;
}

const char *KindName(Kind k) {
	switch (k) {
	case Kind::BrokenLink: return "broken link";
	case Kind::MissingImage: return "missing image";
	case Kind::OrphanMedia: return "unreferenced file";
	case Kind::NoDate: return "no date";
	case Kind::NoSummary: return "no summary";
	case Kind::NoTags: return "no tags";
	case Kind::EmptyBody: return "empty page";
	case Kind::Draft: return "draft";
	default: return "?";
	}
}

const char *KindHelp(Kind k) {
	switch (k) {
	case Kind::BrokenLink:
		return "An internal link with no page behind it. Hugo builds this happily; the reader gets a 404.";
	case Kind::MissingImage:
		return "An image reference with no file behind it, in static/, assets/ or beside the page.";
	case Kind::OrphanMedia:
		return "A file in static/ or assets/ that no page and no setting in hugo.toml points at. It is "
		       "still published -- this is housekeeping, not an error.";
	case Kind::NoDate: return "PaperMod sorts and dates posts from front matter. Without one, ordering is arbitrary.";
	case Kind::NoSummary: return "Hugo will use the opening words in the post list and in link previews.";
	case Kind::NoTags: return "Nothing is broken; the post just isn't reachable from a tag page.";
	case Kind::EmptyBody: return "Front matter and nothing else.";
	case Kind::Draft: return "Left out of a normal build. Listed so a forgotten draft is visible.";
	default: return "";
	}
}

Report Run(const site::Site &s, const std::string &config_text) {
	Report r;
	r.ran = true;

	// ---- what a link is allowed to point at
	std::vector<std::string> pages;
	for (const site::Post &p : s.posts) pages.push_back(p.permalink);
	pages.push_back("/");
	for (const std::string &sec : s.sections) pages.push_back("/" + sec + "/");
	// Taxonomy pages are generated, not written, so they are not in posts.
	for (const site::Post &p : s.posts)
		for (const std::string &t : p.tags) {
			pages.push_back("/tags/" + util::Slugify(t) + "/");
			pages.push_back("/categories/" + util::Slugify(t) + "/");
		}
	pages.push_back("/tags/");
	pages.push_back("/categories/");
	pages.push_back("/archives/");
	auto is_page = [&](const std::string &path) {
		return std::find(pages.begin(), pages.end(), path) != pages.end();
	};

	// ---- what a file reference is allowed to point at
	auto media_for = [&](const std::string &target) -> const site::MediaFile * {
		std::string t = target;
		size_t cut = t.find_first_of("#?");
		if (cut != std::string::npos) t = t.substr(0, cut);
		for (const site::MediaFile &m : s.media) {
			if (m.web == t) return &m;
			// assets/ has no served URL, so a reference names the resource
			// path instead -- and "img/a.png" should find "static/img/a.png"
			// as readily as "/img/a.png" does.
			if (m.rel == t || m.rel == "static/" + t || m.rel == "assets/" + t) return &m;
			if (!t.empty() && t[0] == '/' && m.rel == "static" + t) return &m;
		}
		return nullptr;
	};

	std::vector<std::string> referenced;
	auto mark = [&](const site::MediaFile *m) {
		if (m && std::find(referenced.begin(), referenced.end(), m->rel) == referenced.end())
			referenced.push_back(m->rel);
	};

	// hugo.toml names the avatar, the favicons and the social images. They
	// are referenced by the site even though no post mentions them.
	for (const site::MediaFile &m : s.media)
		if (!config_text.empty() &&
		    (config_text.find(m.web) != std::string::npos || config_text.find(m.rel) != std::string::npos))
			mark(&m);

	// ---- every page
	for (const site::Post &p : s.posts) {
		std::string text;
		if (!util::ReadFile(p.path, &text)) continue;
		r.pages++;
		fm::Doc d = fm::Parse(text);
		std::string where = "content/" + p.rel;

		// Front matter can name a file too (cover.image, images, thumbnail).
		for (const fm::Field &f : d.fields) {
			if (f.v.type == fm::Type::Str) mark(media_for(f.v.s));
			else if (f.v.type == fm::Type::List)
				for (const std::string &v : f.v.list) mark(media_for(v));
		}

		for (const Ref &ref : ExtractRefs(d.body)) {
			if (IsExternal(ref.target)) continue;
			if (ref.image) {
				r.images++;
				const site::MediaFile *m = media_for(ref.target);
				if (m) {
					mark(m);
					continue;
				}
				// A page bundle keeps its images next to index.md.
				std::string beside = util::DirName(p.path) + "/" + ref.target;
				if (util::Exists(beside)) continue;
				Add(&r, Kind::MissingImage, Level::Error, p.path, where, ref.target,
				    "no file at that path, in static/, assets/ or beside the page", ref.line);
				continue;
			}
			r.links++;
			// A link straight at a file that exists is fine (a PDF, an image).
			if (const site::MediaFile *m = media_for(ref.target)) {
				mark(m);
				continue;
			}
			std::string resolved = NormalisePagePath(ResolveRelative(p.permalink, ref.target));
			if (is_page(resolved)) continue;
			Add(&r, Kind::BrokenLink, Level::Error, p.path, where, ref.target,
			    "resolves to " + resolved + ", which no page publishes", ref.line);
		}

		// ---- page-level hygiene
		bool is_post = !p.is_branch && !p.section.empty();
		if (p.draft) Add(&r, Kind::Draft, Level::Info, p.path, where, "", "draft = true", 0);
		if (fm::WordCount(d.body) == 0)
			Add(&r, Kind::EmptyBody, Level::Warn, p.path, where, "", "front matter, and no words after it", 0);
		if (!p.is_branch && p.date.empty())
			Add(&r, Kind::NoDate, Level::Warn, p.path, where, "", "no date in the front matter", 0);
		if (!p.is_branch && p.summary.empty())
			Add(&r, Kind::NoSummary, Level::Info, p.path, where, "", "no summary set", 0);
		if (is_post && p.tags.empty())
			Add(&r, Kind::NoTags, Level::Info, p.path, where, "", "no tags", 0);
	}

	// ---- media nothing points at
	for (const site::MediaFile &m : s.media) {
		if (AlwaysShipped(m.rel)) continue;
		if (std::find(referenced.begin(), referenced.end(), m.rel) != referenced.end()) continue;
		r.unreferenced.push_back(m.rel);
		Add(&r, Kind::OrphanMedia, Level::Info, m.path, m.rel, m.web,
		    "nothing in content/ or hugo.toml refers to this", 0, true);
	}

	// Errors first, then by file, then down the page: the order someone
	// would work through them in.
	std::stable_sort(r.findings.begin(), r.findings.end(), [](const Finding &a, const Finding &b) {
		if (a.level != b.level) return (int)a.level < (int)b.level;
		if (a.where != b.where) return a.where < b.where;
		return a.line < b.line;
	});
	return r;
}

} // namespace audit
