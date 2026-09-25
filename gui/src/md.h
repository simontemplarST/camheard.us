// md -- the Markdown model behind the editor's live preview, plus the text
// transforms its formatting toolbar applies.
//
// Deliberately free of ImGui: everything here is pure string work, which is
// what makes it testable offline (tests/core_test.cpp) without a window.
// The drawing half lives in md_render.{h,cpp}.
//
// The subset covered is the one Hugo + Goldmark actually renders on this
// site: ATX headings, paragraphs, bullet/ordered lists, fenced code,
// blockquotes, pipe tables, thematic breaks, raw HTML (unsafe = true is on),
// Hugo shortcodes, and the inline set (bold, emphasis, code, strike, links,
// images). It is a preview, not a second implementation of Goldmark -- the
// Preview-in-browser button next to it is the authority.
#pragma once

#include <string>
#include <vector>

namespace md {

struct Span {
	std::string text;
	std::string url;   // non-empty for links and images
	bool bold = false;
	bool italic = false;
	bool code = false;
	bool strike = false;
	bool image = false;
};

enum class BlockKind { Paragraph, Heading, List, Code, Quote, Table, Rule, Html, Shortcode };

struct ListItem {
	int indent = 0;          // nesting level, 0 = top
	bool ordered = false;
	std::string marker;      // "1." for ordered items, "-" otherwise
	bool task = false;       // - [ ] / - [x]
	bool checked = false;
	std::vector<Span> spans;
};

struct Block {
	BlockKind kind = BlockKind::Paragraph;
	int level = 0;                              // Heading: 1..6
	std::string lang;                           // Code: the fence's info string
	std::string text;                           // Code/Html/Shortcode: raw text
	std::vector<Span> spans;                    // Paragraph/Heading/Quote
	std::vector<ListItem> items;                // List
	std::vector<std::vector<std::string>> rows; // Table, separator row removed
	bool has_header = false;                    // Table: rows[0] is a header
	int src_line = 0;                           // 0-based line in the source body
};

std::vector<Block> ParseBlocks(const std::string &markdown);
std::vector<Span> ParseInline(const std::string &text);

// ---- toolbar transforms ----------------------------------------------------
// All three take the editor's selection as a byte range [*a, *b) into *text
// and rewrite both the text and the range, so the caret lands somewhere
// sensible afterwards. All three toggle: applying the same action to an
// already-formatted selection removes the formatting instead of nesting it.

void WrapSelection(std::string *text, int *a, int *b, const std::string &open, const std::string &close);
void PrefixLines(std::string *text, int *a, int *b, const std::string &prefix);
void SetHeading(std::string *text, int *a, int *b, int level);
// Inserts at the caret, replacing the selection; leaves the caret after it.
void InsertText(std::string *text, int *a, int *b, const std::string &s);
// A link out of the selection: [selected](url), or [text](url) when empty.
void MakeLink(std::string *text, int *a, int *b, const std::string &url, bool image);

// ---- find and replace ------------------------------------------------------
// Plain text, not regex: the editor's find bar is for fixing a misspelled
// name across a post, and a regex box in a writing tool is a footgun with a
// help page attached.

// Byte offsets of every match, left to right, non-overlapping.
std::vector<int> FindAll(const std::string &hay, const std::string &needle, bool case_sensitive);
// Replaces the match at [*a, *b) -- only when the selection really is that
// match, so "replace" can never silently eat something else. The range ends
// up around the replacement.
bool ReplaceOne(std::string *text, int *a, int *b, const std::string &needle, const std::string &repl,
                bool case_sensitive);
// Replaces every match and returns how many. The range is carried along so
// the caret does not jump to the top of the post afterwards.
int ReplaceAll(std::string *text, int *a, int *b, const std::string &needle, const std::string &repl,
               bool case_sensitive);

// ---- outline ---------------------------------------------------------------

struct Heading {
	int level = 1;      // 1..6
	std::string text;   // the heading's text, markers and trailing #s removed
	int line = 0;       // 0-based line in the source
	int offset = 0;     // byte offset of the start of that line
};

// Every ATX heading in the body, in document order. Headings inside a fenced
// code block are not headings -- a shell prompt in a code sample is the most
// common false positive there is.
std::vector<Heading> Outline(const std::string &markdown);

} // namespace md
