// core_test -- the parsing/editing layer, with no SDL, no GL and no window.
//
// Everything here is offline by construction: the TOML cases build their own
// document text, and the one case that touches the real hugo.toml is skipped
// (not failed) when it isn't there, so this runs from a bare checkout.
//
//   make test
#include "../src/audit.h"
#include "../src/fm.h"
#include "../src/fuzzy.h"
#include "../src/md.h"
#include "../src/site.h"
#include "../src/toml_edit.h"
#include "../src/util.h"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <unistd.h>

static int g_fail = 0, g_pass = 0;

#define CHECK(cond)                                                                      \
	do {                                                                                 \
		if (cond) g_pass++;                                                              \
		else { g_fail++; printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); }       \
	} while (0)

#define CHECK_EQ(a, b)                                                                   \
	do {                                                                                 \
		auto _a = (a);                                                                   \
		auto _b = (b);                                                                   \
		if (_a == _b) g_pass++;                                                          \
		else {                                                                           \
			g_fail++;                                                                    \
			std::string _sa = ToStr(_a), _sb = ToStr(_b);                                \
			printf("  FAIL %s:%d  %s\n     got: <%s>\n    want: <%s>\n", __FILE__,       \
			       __LINE__, #a " == " #b, _sa.c_str(), _sb.c_str());                    \
		}                                                                                \
	} while (0)

static std::string ToStr(const std::string &s) { return s; }
static std::string ToStr(const char *s) { return s; }
static std::string ToStr(int v) { return std::to_string(v); }
static std::string ToStr(size_t v) { return std::to_string(v); }
static std::string ToStr(bool v) { return v ? "true" : "false"; }
static std::string ToStr(double v) { return std::to_string(v); }

static void section(const char *name) { printf("== %s\n", name); }

// ---------------------------------------------------------------- front matter

static void test_fm_toml() {
	section("fm: TOML front matter");
	const std::string src =
	    "+++\n"
	    "date = '2026-09-01T21:42:44-05:00'\n"
	    "draft = true\n"
	    "title = \"Test post from tui\"\n"
	    "summary = ''\n"
	    "tags = [\"linux\", \"hugo\"]\n"
	    "weight = 5\n"
	    "+++\n"
	    "test test 123\n";
	fm::Doc d = fm::Parse(src);
	CHECK(d.kind == fm::Kind::TOML);
	CHECK_EQ(d.GetStr("title"), "Test post from tui");
	CHECK_EQ(d.GetBool("draft"), true);
	CHECK_EQ(d.GetStr("date"), "2026-09-01T21:42:44-05:00"); // quoted -> stays a string
	CHECK_EQ(d.GetList("tags").size(), (size_t)2);
	CHECK_EQ(d.GetList("tags")[1], "hugo");
	CHECK_EQ(d.GetNum("weight", -1), 5.0);
	CHECK_EQ(d.body, "test test 123\n");

	// Round trip: every key survives, in order, and the body is untouched.
	fm::Doc r = fm::Parse(fm::Serialize(d));
	CHECK_EQ(r.fields.size(), d.fields.size());
	CHECK_EQ(r.GetStr("date"), "2026-09-01T21:42:44-05:00");
	CHECK_EQ(r.GetBool("draft"), true);
	CHECK_EQ(r.body, "test test 123\n");
	CHECK_EQ(r.fields[0].key, "date");

	d.SetBool("draft", false);
	d.SetList("tags", {"a"});
	std::string out = fm::Serialize(d);
	CHECK(out.find("draft = false") != std::string::npos);
	CHECK(out.find("tags = [\"a\"]") != std::string::npos);
}

static void test_fm_yaml() {
	section("fm: YAML front matter stays YAML");
	const std::string src =
	    "---\n"
	    "title: \"Hello\"\n"
	    "draft: false\n"
	    "tags:\n"
	    "  - linux\n"
	    "  - systemd\n"
	    "---\n"
	    "\n"
	    "Body text.\n";
	fm::Doc d = fm::Parse(src);
	CHECK(d.kind == fm::Kind::YAML);
	CHECK_EQ(d.GetStr("title"), "Hello");
	CHECK_EQ(d.GetList("tags").size(), (size_t)2);
	CHECK_EQ(d.GetList("tags")[0], "linux");
	CHECK_EQ(d.body, "Body text.\n");
	std::string out = fm::Serialize(d);
	CHECK(util::StartsWith(out, "---\n"));
	CHECK(out.find("title: \"Hello\"") != std::string::npos);
}

static void test_fm_edges() {
	section("fm: files with no front matter, and unterminated fences");
	fm::Doc plain = fm::Parse("just a body\n");
	CHECK(plain.kind == fm::Kind::None);
	CHECK_EQ(plain.body, "just a body\n");
	CHECK_EQ(fm::Serialize(plain), "just a body\n");

	// An unterminated fence must not swallow the post into front matter.
	fm::Doc bad = fm::Parse("+++\ntitle = \"x\"\nno closing fence\n");
	CHECK(bad.kind == fm::Kind::None);
	CHECK(bad.body.find("no closing fence") != std::string::npos);

	CHECK_EQ(fm::WordCount("one two three\n"), 3);
	CHECK_EQ(fm::WordCount("prose\n```\ncode words here\n```\nmore\n"), 2);
	CHECK_EQ(fm::WordCount("a {{< figure src=\"x\" >}} b\n"), 2);
}

// --------------------------------------------------------------------- toml

static const char *kSampleToml =
    "baseURL = \"https://camheard.us/\"\n"
    "title = \"camheard.us\"\n"
    "theme = \"PaperMod\"\n"
    "\n"
    "[params]\n"
    "  env = \"production\"\n"
    "  description = \"A blog about Linux.\"\n"
    "  ShowToc = true          # table of contents on posts\n"
    "  # imageUrl = \"img/avatar.png\"   # drop a file at assets/img/avatar.png\n"
    "\n"
    "  [params.profileMode]\n"
    "    enabled = true\n"
    "    title = \"Cameron Heard\"\n"
    "\n"
    "  [[params.socialIcons]]\n"
    "    name = \"github\"\n"
    "    url = \"https://github.com/simontemplarST\"\n"
    "  [[params.socialIcons]]\n"
    "    name = \"rss\"\n"
    "    url = \"index.xml\"\n"
    "\n"
    "[menu]\n"
    "  [[menu.main]]\n"
    "    identifier = \"posts\"\n"
    "    name = \"Posts\"\n"
    "    weight = 10\n";

static void test_toml_read() {
	section("toml: reading dotted paths");
	tomledit::Doc t;
	t.SetText(kSampleToml);
	CHECK_EQ(t.GetString("title"), "camheard.us");
	CHECK_EQ(t.GetString("params.description"), "A blog about Linux.");
	CHECK_EQ(t.GetString("params.profileMode.title"), "Cameron Heard");
	CHECK_EQ(t.GetBool("params.ShowToc"), true);
	// Passing the opposite default proves the value was really read, not
	// defaulted: enabled = true in the document, so a default of false must
	// still come back true.
	CHECK_EQ(t.GetBool("params.profileMode.enabled", false), true);
	CHECK_EQ(t.GetBool("params.missingFlag", true), true); // absent -> default
	CHECK(t.Has("params.env"));
	CHECK(!t.Has("params.nope"));
	// A key under [[params.socialIcons]] must not answer a scalar lookup.
	CHECK(!t.Has("params.socialIcons.name"));
	// A commented-out key is not a key.
	CHECK(!t.Has("params.profileMode.imageUrl"));
	CHECK(!t.Has("params.imageUrl"));
}

static void test_toml_write_preserves() {
	section("toml: writing keeps comments, indentation and line count");
	tomledit::Doc t;
	t.SetText(kSampleToml);
	size_t before = t.lines.size();
	t.SetString("title", "New Title");
	t.SetBool("params.ShowToc", false);
	t.SetString("params.profileMode.title", "Cam");
	CHECK_EQ(t.lines.size(), before); // in-place: no line added
	CHECK_EQ(t.GetString("title"), "New Title");
	CHECK_EQ(t.GetBool("params.ShowToc", true), false);
	CHECK_EQ(t.GetString("params.profileMode.title"), "Cam");
	std::string out = t.Text();
	CHECK(out.find("# table of contents on posts") != std::string::npos); // trailing comment kept
	CHECK(out.find("# imageUrl = \"img/avatar.png\"") != std::string::npos);
	CHECK(out.find("    title = \"Cam\"") != std::string::npos);          // indentation kept
	CHECK(out.find("[params.profileMode]") != std::string::npos);
}

static void test_toml_insert() {
	section("toml: inserting a key that isn't there yet");
	tomledit::Doc t;
	t.SetText(kSampleToml);
	t.SetString("params.profileMode.subtitle", "hello");
	CHECK_EQ(t.GetString("params.profileMode.subtitle"), "hello");
	// It must land inside [params.profileMode], i.e. before the socialIcons
	// blocks -- not appended to the end of the file under [menu].
	std::string out = t.Text();
	size_t at = out.find("subtitle = \"hello\"");
	CHECK(at != std::string::npos);
	CHECK(at > out.find("[params.profileMode]"));
	CHECK(at < out.find("[[params.socialIcons]]"));

	t.SetBool("params.newFlag", true);
	CHECK_EQ(t.GetBool("params.newFlag"), true);
	size_t nf = t.Text().find("newFlag");
	CHECK(nf > t.Text().find("[params]"));
	CHECK(nf < t.Text().find("[params.profileMode]"));

	// A brand-new table gets created at the end.
	t.SetString("outputs.home", "x");
	CHECK_EQ(t.GetString("outputs.home"), "x");

	// Root-level insert with no table of its own.
	t.SetString("languageCode", "en-us");
	CHECK_EQ(t.GetString("languageCode"), "en-us");
	size_t lc = t.Text().find("languageCode");
	CHECK(lc < t.Text().find("[params]"));
}

static void test_toml_tables() {
	section("toml: arrays of tables (social icons, menu)");
	tomledit::Doc t;
	t.SetText(kSampleToml);
	auto icons = t.GetTables("params.socialIcons");
	CHECK_EQ(icons.size(), (size_t)2);
	CHECK_EQ(icons[0].GetString("name"), "github");
	CHECK_EQ(icons[1].GetString("url"), "index.xml");

	// Reorder + add, then read back.
	std::vector<tomledit::Doc::Table> next;
	next.push_back(icons[1]);
	next.push_back(icons[0]);
	tomledit::Doc::Table extra;
	extra.Set("name", tomledit::Quote("email"));
	extra.Set("url", tomledit::Quote("mailto:a@b.c"));
	next.push_back(extra);
	t.SetTables("params.socialIcons", next);

	auto after = t.GetTables("params.socialIcons");
	CHECK_EQ(after.size(), (size_t)3);
	CHECK_EQ(after[0].GetString("name"), "rss");
	CHECK_EQ(after[2].GetString("url"), "mailto:a@b.c");
	// The surrounding file is intact.
	CHECK(t.Text().find("[menu]") != std::string::npos);
	CHECK(t.Text().find("[params.profileMode]") != std::string::npos);
	CHECK_EQ(t.GetString("params.profileMode.title"), "Cameron Heard");

	auto menu = t.GetTables("menu.main");
	CHECK_EQ(menu.size(), (size_t)1);
	CHECK_EQ(menu[0].GetString("name"), "Posts");
	CHECK_EQ(menu[0].Get("weight"), "10");

	// Emptying a list removes every block.
	t.SetTables("params.socialIcons", {});
	CHECK_EQ(t.GetTables("params.socialIcons").size(), (size_t)0);
	CHECK(t.Text().find("[params.profileMode]") != std::string::npos);
}

static void test_toml_real_file() {
	section("toml: the site's own hugo.toml");
	tomledit::Doc t;
	if (!t.Load("../hugo.toml")) {
		printf("  skip (no ../hugo.toml -- running outside the site)\n");
		return;
	}
	CHECK_EQ(t.GetString("theme"), "PaperMod");
	CHECK(!t.GetString("baseURL").empty());
	CHECK_EQ(t.GetTables("menu.main").size(), (size_t)4);
	CHECK_EQ(t.GetTables("params.socialIcons").size(), (size_t)2);
	CHECK_EQ(t.GetTables("params.profileMode.buttons").size(), (size_t)2);
	CHECK_EQ(t.GetString("params.profileMode.title"), "Cameron Heard");
	CHECK_EQ(t.GetNum("markup.highlight.tabWidth", -1), 4.0);

	// Byte-identical unless something is actually changed.
	std::string original;
	util::ReadFile("../hugo.toml", &original);
	CHECK_EQ(t.Text(), original);
}

// --------------------------------------------------------------------- misc

static void test_util() {
	section("util");
	CHECK_EQ(util::Slugify("My First Post!"), "my-first-post");
	CHECK_EQ(util::Slugify("  spaces   and--dashes "), "spaces-and-dashes");
	CHECK_EQ(util::StripExt("a/b/c.md"), "a/b/c");
	CHECK_EQ(util::BaseName("a/b/c.md"), "c.md");
	CHECK_EQ(util::Join({"a", "b"}, ", "), "a, b");
	CHECK_EQ(util::SplitLines("a\nb\n").size(), (size_t)2);
	CHECK_EQ(util::SplitLines("a\nb").size(), (size_t)2);
	CHECK_EQ(util::PrettyDate("2026-09-01T21:42:44-05:00"), "2026-09-01 21:42");
}

static void test_permalink() {
	section("site: permalinks");
	CHECK_EQ(site::Permalink("posts/hello-world.md", ""), "/posts/hello-world/");
	CHECK_EQ(site::Permalink("posts/nested/_index.md", ""), "/posts/nested/");
	CHECK_EQ(site::Permalink("about.md", "/about/"), "/about/");
	CHECK_EQ(site::Permalink("posts/a/index.md", ""), "/posts/a/");
}

static void test_markdown_blocks() {
	section("markdown: block parsing");
	std::vector<md::Block> b = md::ParseBlocks(
	    "# Title\n"
	    "\n"
	    "Some *text* with `code`.\n"
	    "\n"
	    "- one\n"
	    "- two\n"
	    "\n"
	    "```bash\n"
	    "echo hi\n"
	    "```\n"
	    "\n"
	    "> quoted\n"
	    "\n"
	    "| a | b |\n"
	    "|---|---|\n"
	    "| 1 | 2 |\n");
	CHECK_EQ(b.size(), (size_t)6);
	CHECK(b[0].kind == md::BlockKind::Heading);
	CHECK_EQ(b[0].level, 1);
	CHECK(b[1].kind == md::BlockKind::Paragraph);
	CHECK(b[2].kind == md::BlockKind::List);
	CHECK_EQ(b[2].items.size(), (size_t)2);
	CHECK(b[3].kind == md::BlockKind::Code);
	CHECK_EQ(b[3].lang, "bash");
	CHECK_EQ(b[3].text, "echo hi\n");
	CHECK(b[4].kind == md::BlockKind::Quote);
	CHECK(b[5].kind == md::BlockKind::Table);
	CHECK_EQ(b[5].rows.size(), (size_t)2);
	CHECK_EQ(b[5].rows[1][1], "2");

	// A fenced block must not have its contents parsed as markdown.
	std::vector<md::Block> f = md::ParseBlocks("```\n# not a heading\n- not a list\n```\n");
	CHECK_EQ(f.size(), (size_t)1);
	CHECK(f[0].kind == md::BlockKind::Code);
	CHECK(f[0].text.find("# not a heading") != std::string::npos);
}

static void test_markdown_inline() {
	section("markdown: inline spans");
	std::vector<md::Span> s = md::ParseInline("a **bold** and *em* and `c` and [t](u)");
	std::string plain;
	bool saw_bold = false, saw_em = false, saw_code = false;
	std::string link_url;
	for (const md::Span &sp : s) {
		plain += sp.text;
		if (sp.bold) saw_bold = true;
		if (sp.italic) saw_em = true;
		if (sp.code) saw_code = true;
		if (!sp.url.empty()) link_url = sp.url;
	}
	CHECK_EQ(plain, "a bold and em and c and t");
	CHECK(saw_bold);
	CHECK(saw_em);
	CHECK(saw_code);
	CHECK_EQ(link_url, "u");

	// Emphasis markers inside code must be left alone.
	std::vector<md::Span> c = md::ParseInline("`a *b* c`");
	CHECK_EQ(c.size(), (size_t)1);
	CHECK(c[0].code);
	CHECK_EQ(c[0].text, "a *b* c");

	// An unmatched marker is literal text, not a dangling style.
	std::vector<md::Span> u = md::ParseInline("2 * 3 * 4");
	std::string up;
	for (const md::Span &sp : u) up += sp.text;
	CHECK_EQ(up, "2 * 3 * 4");
}

static void test_markdown_transform() {
	section("markdown: the toolbar's text transforms");
	std::string text = "hello world";
	int a = 0, b = 5;
	md::WrapSelection(&text, &a, &b, "**", "**");
	CHECK_EQ(text, "**hello** world");
	CHECK_EQ(a, 2);
	CHECK_EQ(b, 7);
	// Toggling off removes the markers again.
	md::WrapSelection(&text, &a, &b, "**", "**");
	CHECK_EQ(text, "hello world");
	CHECK_EQ(a, 0);
	CHECK_EQ(b, 5);

	// With an empty selection it inserts the markers and puts the caret
	// between them, so typing continues inside.
	std::string t2 = "x";
	int c = 1, d = 1;
	md::WrapSelection(&t2, &c, &d, "**", "**");
	CHECK_EQ(t2, "x****");
	CHECK_EQ(c, 3);

	std::string t3 = "one\ntwo\n";
	int e = 0, f = 8;
	md::PrefixLines(&t3, &e, &f, "- ");
	CHECK_EQ(t3, "- one\n- two\n");
	md::PrefixLines(&t3, &e, &f, "- "); // toggles back off
	CHECK_EQ(t3, "one\ntwo\n");

	std::string t4 = "Title\n";
	int g = 0, h = 0;
	md::SetHeading(&t4, &g, &h, 2);
	CHECK_EQ(t4, "## Title\n");
	md::SetHeading(&t4, &g, &h, 2);
	CHECK_EQ(t4, "Title\n"); // same level again clears it
	md::SetHeading(&t4, &g, &h, 3);
	CHECK_EQ(t4, "### Title\n");
	md::SetHeading(&t4, &g, &h, 1);
	CHECK_EQ(t4, "# Title\n"); // a different level replaces, never stacks
}


// ------------------------------------------------------------------- find

static void test_find_replace() {
	section("markdown: find and replace");
	const std::string hay = "Hugo and hugo and HUGO";
	CHECK_EQ(md::FindAll(hay, "hugo", false).size(), (size_t)3);
	CHECK_EQ(md::FindAll(hay, "hugo", true).size(), (size_t)1);
	CHECK_EQ(md::FindAll(hay, "hugo", false)[1], 9);
	CHECK_EQ(md::FindAll(hay, "", false).size(), (size_t)0);
	// Non-overlapping: "aa" appears twice in "aaaa", not three times.
	CHECK_EQ(md::FindAll("aaaa", "aa", false).size(), (size_t)2);

	// ReplaceOne only fires when the selection really is the match, so a
	// stale selection can never eat the wrong text.
	std::string t = "the cat sat";
	int a = 4, b = 7;
	CHECK(md::ReplaceOne(&t, &a, &b, "cat", "dog", false));
	CHECK_EQ(t, "the dog sat");
	CHECK_EQ(a, 4);
	CHECK_EQ(b, 7);
	a = 0; b = 3;
	CHECK(!md::ReplaceOne(&t, &a, &b, "cat", "dog", false));
	CHECK_EQ(t, "the dog sat");

	std::string t2 = "one two one two";
	int c = 15, d = 15;   // caret at the very end
	CHECK_EQ(md::ReplaceAll(&t2, &c, &d, "one", "three", false), 2);
	CHECK_EQ(t2, "three two three two");
	CHECK_EQ(c, 19);      // the caret followed the text that grew before it

	// A caret sitting inside a replaced match lands after its replacement
	// rather than somewhere in the middle of the new word.
	std::string t3 = "alpha beta";
	int e = 2, f = 2;
	CHECK_EQ(md::ReplaceAll(&t3, &e, &f, "alpha", "x", false), 1);
	CHECK_EQ(t3, "x beta");
	CHECK_EQ(e, 1);

	// Case sensitivity is honoured in both directions.
	std::string t4 = "Cat cat";
	int g = 0, h = 0;
	CHECK_EQ(md::ReplaceAll(&t4, &g, &h, "cat", "dog", true), 1);
	CHECK_EQ(t4, "Cat dog");
}

static void test_outline() {
	section("markdown: outline");
	const std::string body =
	    "intro paragraph\n"
	    "# Title\n"
	    "words\n"
	    "## Second ##\n"
	    "```sh\n"
	    "# not a heading, it is a shell comment\n"
	    "```\n"
	    "### Third\n"
	    "#hashtag is not one either\n";
	std::vector<md::Heading> o = md::Outline(body);
	CHECK_EQ(o.size(), (size_t)3);
	CHECK_EQ(o[0].text, "Title");
	CHECK_EQ(o[0].level, 1);
	CHECK_EQ(o[0].line, 1);
	CHECK_EQ(body.substr(o[0].offset, 7), "# Title");
	CHECK_EQ(o[1].text, "Second");   // the closing #s are decoration
	CHECK_EQ(o[1].level, 2);
	CHECK_EQ(o[2].text, "Third");
	CHECK_EQ(o[2].level, 3);
	CHECK_EQ(body.substr(o[2].offset, 9), "### Third");
}

// ------------------------------------------------------------------ fuzzy

static void test_fuzzy() {
	section("fuzzy: the palette's ranking");
	CHECK(fuzzy::Score("about", "abt").hit);
	CHECK(!fuzzy::Score("about", "abz").hit);
	CHECK(fuzzy::Score("anything", "").hit);        // empty needle lists everything
	CHECK(!fuzzy::Score("", "a").hit);
	CHECK(fuzzy::Score("About The Site", "abt").hit); // case-insensitive

	// The tightened pass: "abt" in "about" matches the a-b-t that ends
	// where the first match ends, not the leftmost spread-out one.
	std::vector<int> pos = fuzzy::Score("about", "abt").pos;
	CHECK_EQ(pos.size(), (size_t)3);
	CHECK_EQ(pos[2], 4);

	// Ranking: the whole point. A prefix beats a scattered match, a
	// word-boundary match beats a mid-word one, and a run of letters beats
	// the same letters spread out.
	CHECK(fuzzy::Score("posts/hello.md", "hello").score >
	      fuzzy::Score("hollow shell below", "hello").score);
	CHECK(fuzzy::Score("content/posts/setup.md", "setup").score >
	      fuzzy::Score("content/posts/sadly-eating-up.md", "setup").score);
	CHECK(fuzzy::Score("New post", "np").score > fuzzy::Score("unwrap", "np").score);
}

// ------------------------------------------------------------------ audit

static void test_audit_refs() {
	section("audit: pulling links out of a body");
	const std::string body =
	    "A [link](/posts/one/) and an ![image](/img/a.png).\n"
	    "`[code](/not/a/link/)` stays out of it.\n"
	    "```\n[fenced](/nor/this/)\n```\n"
	    "<img src=\"/img/b.png\"> and <a href='/about/'>about</a>\n"
	    "[ref]: /posts/two/\n";
	std::vector<audit::Ref> refs = audit::ExtractRefs(body);
	CHECK_EQ(refs.size(), (size_t)5);
	CHECK_EQ(refs[0].target, "/posts/one/");
	CHECK(!refs[0].image);
	CHECK_EQ(refs[0].line, 1);
	CHECK_EQ(refs[1].target, "/img/a.png");
	CHECK(refs[1].image);
	CHECK_EQ(refs[2].target, "/img/b.png");
	CHECK(refs[2].image);
	CHECK_EQ(refs[3].target, "/about/");
	CHECK_EQ(refs[4].target, "/posts/two/");

	// A title after the URL is not part of it, and <> wrappers come off.
	std::vector<audit::Ref> r2 = audit::ExtractRefs("[a](<{{< ref \"x\" >}}>) [b](/c/ \"title\")\n");
	CHECK_EQ(r2.size(), (size_t)2);
	CHECK_EQ(r2[1].target, "/c/");

	CHECK(audit::IsExternal("https://example.com"));
	CHECK(audit::IsExternal("#anchor"));
	CHECK(audit::IsExternal("mailto:a@b.c"));
	CHECK(audit::IsExternal("{{< ref \"x\" >}}"));
	CHECK(!audit::IsExternal("/posts/one/"));
}

static void test_audit_run() {
	section("audit: a whole site");
	// A site built in a temp directory, so this stays offline and leaves
	// nothing behind.
	const char *tmp = getenv("TMPDIR");
	std::string root = std::string(tmp && *tmp ? tmp : "/tmp") + "/hugofe-audit-test";
	util::MakeDirs(root + "/content/posts");
	util::MakeDirs(root + "/static/img");
	util::WriteFileAtomic(root + "/hugo.toml", "baseURL = 'https://example.com/'\n");
	util::WriteFileAtomic(root + "/static/img/used.png", "x");
	util::WriteFileAtomic(root + "/static/img/orphan.png", "x");
	util::WriteFileAtomic(root + "/static/CNAME", "example.com");
	util::WriteFileAtomic(root + "/content/posts/one.md",
	                      "+++\ntitle = 'One'\ndate = '2026-01-01T00:00:00Z'\nsummary = 's'\n"
	                      "tags = ['a']\n+++\n"
	                      "![](/img/used.png)\n"
	                      "![](/img/gone.png)\n"
	                      "[good](/posts/two/)\n"
	                      "[bad](/posts/nowhere/)\n"
	                      "[home](/) and [tag](/tags/a/) and [out](https://example.org/)\n");
	util::WriteFileAtomic(root + "/content/posts/two.md", "+++\ntitle = 'Two'\ndraft = true\n+++\n");

	site::Site s;
	CHECK(s.Detect(root));
	s.Scan();
	s.ScanMedia();
	std::string cfg;
	util::ReadFile(s.config, &cfg);
	audit::Report rep = audit::Run(s, cfg);

	CHECK_EQ(rep.pages, 2);
	CHECK_EQ(rep.Count(audit::Kind::MissingImage), 1);
	CHECK_EQ(rep.Count(audit::Kind::BrokenLink), 1);
	// /posts/two/, /, and /tags/a/ all resolve; the external one is skipped.
	CHECK_EQ(rep.Count(audit::Kind::Draft), 1);
	CHECK_EQ(rep.Count(audit::Kind::EmptyBody), 1);
	CHECK_EQ(rep.Count(audit::Kind::NoDate), 1);   // two.md has none
	CHECK_EQ(rep.Count(audit::Kind::NoTags), 1);

	// used.png is referenced, orphan.png is not, CNAME is never counted.
	CHECK(!rep.Unreferenced("static/img/used.png"));
	CHECK(rep.Unreferenced("static/img/orphan.png"));
	CHECK(!rep.Unreferenced("static/CNAME"));

	// Errors sort to the top, so the first thing in the list is worth fixing.
	CHECK(!rep.findings.empty());
	CHECK(rep.findings[0].level == audit::Level::Error);
	CHECK_EQ(rep.errors, 2);

	// A file named only by hugo.toml counts as referenced -- that is how the
	// avatar and the favicons survive.
	util::WriteFileAtomic(s.config, "baseURL = 'https://example.com/'\nimageUrl = '/img/orphan.png'\n");
	util::ReadFile(s.config, &cfg);
	audit::Report rep2 = audit::Run(s, cfg);
	CHECK(!rep2.Unreferenced("static/img/orphan.png"));

	// Clean up: a test that leaves files behind fails differently next run.
	for (const char *f : {"/content/posts/one.md", "/content/posts/two.md", "/static/img/used.png",
	                      "/static/img/orphan.png", "/static/CNAME", "/hugo.toml"})
		remove((root + f).c_str());
	for (const char *d : {"/content/posts", "/content", "/static/img", "/static", ""})
		rmdir((root + d).c_str());
}

int main() {
	test_util();
	test_fm_toml();
	test_fm_yaml();
	test_fm_edges();
	test_toml_read();
	test_toml_write_preserves();
	test_toml_insert();
	test_toml_tables();
	test_toml_real_file();
	test_permalink();
	test_markdown_blocks();
	test_markdown_inline();
	test_markdown_transform();
	test_find_replace();
	test_outline();
	test_fuzzy();
	test_audit_refs();
	test_audit_run();
	printf("\n%d passed, %d failed\n", g_pass, g_fail);
	return g_fail ? 1 : 0;
}
