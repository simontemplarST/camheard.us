// audit -- one pass over the whole site, looking for the things that are
// only discovered after publishing: a link to a page that was renamed, an
// image whose file never got committed, a file in static/ that nothing
// points at any more.
//
// Hugo builds all of these without complaint -- a dead internal link is a
// 404 for the reader and a silent success for the build -- so the check has
// to live here rather than in `hugo`'s output.
//
// ImGui-free on purpose, like everything else `make test` covers: this is
// filesystem plus string work, and the Check tab is only a table of what it
// returns.
#pragma once

#include "site.h"

#include <string>
#include <vector>

namespace audit {

enum class Kind {
	BrokenLink,   // /posts/old-name/ with no page behind it
	MissingImage, // ![](/img/x.png) with no such file
	OrphanMedia,  // static/img/x.png that nothing references
	NoDate,
	NoSummary,
	NoTags,
	EmptyBody,
	Draft,
	COUNT
};

enum class Level { Error, Warn, Info };

struct Finding {
	Kind kind = Kind::BrokenLink;
	Level level = Level::Warn;
	std::string path;    // absolute, so the UI can open it
	std::string where;   // how to print it: "content/posts/x.md", "static/img/a.png"
	std::string subject; // the offending target, empty for whole-page findings
	std::string detail;  // one sentence, addressed to whoever has to fix it
	int line = 0;        // 1-based line in the source; 0 = the file as a whole
	bool is_media = false;
};

struct Ref {
	std::string target;
	bool image = false;
	int line = 0; // 1-based
};

// Every link and image target in a markdown body, in document order.
// Fenced code blocks and inline code spans are skipped: a URL in a code
// sample is an example, not a link.
std::vector<Ref> ExtractRefs(const std::string &body);

// True for targets this check has nothing useful to say about: absolute
// URLs, protocol-relative ones, mailto:/tel:, bare anchors, and anything
// still holding a Hugo template expression.
bool IsExternal(const std::string &target);

struct Report {
	std::vector<Finding> findings;
	int errors = 0, warnings = 0, infos = 0;
	std::vector<std::string> unreferenced; // media rel paths nothing points at
	int pages = 0, links = 0, images = 0;
	bool ran = false;

	bool Unreferenced(const std::string &media_rel) const;
	int Count(Kind k) const;
};

// Re-reads every page under content/ and cross-references it against the
// scanned media list and hugo.toml's own text (the avatar and the favicons
// are referenced from there, not from any post).
Report Run(const site::Site &s, const std::string &config_text);

// True for files that ship because a browser or a host asks for them by
// name -- CNAME, robots.txt, the favicons -- rather than because anything
// links to them. They are never reported as unreferenced.
bool AlwaysShipped(const std::string &media_rel);

const char *KindName(Kind k);
const char *KindHelp(Kind k);

} // namespace audit
