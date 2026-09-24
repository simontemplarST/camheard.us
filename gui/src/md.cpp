#include "md.h"

#include "util.h"

#include <cctype>

namespace md {
namespace {

bool IsRule(const std::string &t) {
	if (t.size() < 3) return false;
	char c = t[0];
	if (c != '-' && c != '*' && c != '_') return false;
	for (char x : t)
		if (x != c && x != ' ') return false;
	return true;
}

int HeadingLevel(const std::string &t, std::string *rest) {
	int n = 0;
	while (n < (int)t.size() && t[n] == '#') n++;
	if (n == 0 || n > 6) return 0;
	if (n < (int)t.size() && t[n] != ' ') return 0; // "#hashtag" is not a heading
	*rest = util::Trim(t.substr(n));
	return n;
}

// "- ", "* ", "+ ", "1. ", "1) ". Returns the marker length, 0 when the line
// is not a list item.
size_t ListMarker(const std::string &t, bool *ordered, std::string *marker) {
	if (t.size() >= 2 && (t[0] == '-' || t[0] == '*' || t[0] == '+') && t[1] == ' ') {
		*ordered = false;
		*marker = std::string(1, t[0]);
		return 2;
	}
	size_t i = 0;
	while (i < t.size() && std::isdigit((unsigned char)t[i])) i++;
	if (i > 0 && i + 1 < t.size() && (t[i] == '.' || t[i] == ')') && t[i + 1] == ' ') {
		*ordered = true;
		*marker = t.substr(0, i + 1);
		return i + 2;
	}
	return 0;
}

std::vector<std::string> SplitRow(const std::string &line) {
	std::string t = util::Trim(line);
	if (!t.empty() && t.front() == '|') t = t.substr(1);
	if (!t.empty() && t.back() == '|') t.pop_back();
	std::vector<std::string> cells;
	std::string cur;
	for (size_t i = 0; i < t.size(); i++) {
		if (t[i] == '\\' && i + 1 < t.size() && t[i + 1] == '|') { cur += '|'; i++; continue; }
		if (t[i] == '|') { cells.push_back(util::Trim(cur)); cur.clear(); }
		else cur += t[i];
	}
	cells.push_back(util::Trim(cur));
	return cells;
}

bool IsTableSeparator(const std::string &line) {
	std::string t = util::Trim(line);
	if (t.find('|') == std::string::npos) return false;
	for (char c : t)
		if (c != '|' && c != '-' && c != ':' && c != ' ') return false;
	return t.find('-') != std::string::npos;
}

size_t LeadingSpaces(const std::string &s) {
	size_t i = 0;
	while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) i += (s[i] == '\t') ? 4 : 1;
	return i;
}

} // namespace

// ---- inline ---------------------------------------------------------------

static void ParseInto(const std::string &s, Span style, std::vector<Span> &out) {
	std::string acc;
	auto flush = [&]() {
		if (acc.empty()) return;
		Span sp = style;
		sp.text = acc;
		out.push_back(sp);
		acc.clear();
	};

	for (size_t i = 0; i < s.size();) {
		char c = s[i];

		// `code` -- wins over every other marker, so `a *b* c` stays literal.
		if (c == '`') {
			size_t run = 1;
			while (i + run < s.size() && s[i + run] == '`') run++;
			std::string fence(run, '`');
			size_t end = s.find(fence, i + run);
			if (end != std::string::npos) {
				flush();
				Span sp = style;
				sp.code = true;
				sp.text = util::Trim(s.substr(i + run, end - (i + run)));
				out.push_back(sp);
				i = end + run;
				continue;
			}
		}

		// ![alt](src) and [text](url)
		if ((c == '[' || (c == '!' && i + 1 < s.size() && s[i + 1] == '[')) && style.code == false) {
			size_t open = (c == '!') ? i + 1 : i;
			int depth = 0;
			size_t close = std::string::npos;
			for (size_t j = open; j < s.size(); j++) {
				if (s[j] == '[') depth++;
				else if (s[j] == ']') { depth--; if (depth == 0) { close = j; break; } }
			}
			if (close != std::string::npos && close + 1 < s.size() && s[close + 1] == '(') {
				size_t pend = s.find(')', close + 2);
				if (pend != std::string::npos) {
					flush();
					std::string label = s.substr(open + 1, close - open - 1);
					std::string url = util::Trim(s.substr(close + 2, pend - close - 2));
					// A title after the URL ("url \"title\"") is not shown.
					size_t sp_at = url.find(' ');
					if (sp_at != std::string::npos) url = url.substr(0, sp_at);
					Span inner = style;
					inner.url = url;
					if (c == '!') {
						inner.image = true;
						inner.text = label;
						out.push_back(inner);
					} else {
						ParseInto(label, inner, out);
					}
					i = pend + 1;
					continue;
				}
			}
		}

		// ~~strike~~
		if (c == '~' && i + 1 < s.size() && s[i + 1] == '~') {
			size_t end = s.find("~~", i + 2);
			if (end != std::string::npos && end > i + 2) {
				flush();
				Span inner = style;
				inner.strike = true;
				ParseInto(s.substr(i + 2, end - i - 2), inner, out);
				i = end + 2;
				continue;
			}
		}

		// **strong**, *em*, __strong__, _em_
		if (c == '*' || c == '_') {
			size_t run = 1;
			while (i + run < s.size() && s[i + run] == c) run++;
			if (run > 2) run = 2;
			std::string marker(run, c);
			// An opener is never followed by whitespace -- that is what keeps
			// "2 * 3 * 4" literal rather than turning into emphasis.
			if (i + run < s.size() && !std::isspace((unsigned char)s[i + run])) {
				size_t search = i + run;
				size_t end = std::string::npos;
				while ((search = s.find(marker, search)) != std::string::npos) {
					if (search > 0 && std::isspace((unsigned char)s[search - 1])) { search += run; continue; }
					// For a single-char marker, don't match half of a double.
					if (run == 1 && search + 1 < s.size() && s[search + 1] == c) { search += 2; continue; }
					end = search;
					break;
				}
				if (end != std::string::npos && end > i + run) {
					flush();
					Span inner = style;
					if (run == 2) inner.bold = true;
					else inner.italic = true;
					ParseInto(s.substr(i + run, end - i - run), inner, out);
					i = end + run;
					continue;
				}
			}
		}

		acc += c;
		i++;
	}
	flush();
}

std::vector<Span> ParseInline(const std::string &text) {
	std::vector<Span> out;
	ParseInto(text, Span{}, out);
	return out;
}

// ---- blocks ---------------------------------------------------------------

std::vector<Block> ParseBlocks(const std::string &markdown) {
	std::vector<Block> out;
	std::vector<std::string> lines = util::SplitLines(markdown);

	for (size_t i = 0; i < lines.size();) {
		const std::string &raw = lines[i];
		std::string t = util::Trim(raw);

		if (t.empty()) { i++; continue; }

		// Fenced code. Everything inside is literal -- a `# comment` in a
		// shell block is not a heading.
		if (util::StartsWith(t, "```") || util::StartsWith(t, "~~~")) {
			std::string fence = t.substr(0, 3);
			Block b;
			b.kind = BlockKind::Code;
			b.src_line = (int)i;
			b.lang = util::Trim(t.substr(3));
			size_t j = i + 1;
			for (; j < lines.size(); j++) {
				if (util::StartsWith(util::Trim(lines[j]), fence)) break;
				b.text += lines[j];
				b.text += "\n";
			}
			out.push_back(b);
			i = (j < lines.size()) ? j + 1 : j;
			continue;
		}

		if (IsRule(t)) {
			Block b;
			b.kind = BlockKind::Rule;
			b.src_line = (int)i;
			out.push_back(b);
			i++;
			continue;
		}

		std::string rest;
		int lvl = HeadingLevel(t, &rest);
		if (lvl) {
			Block b;
			b.kind = BlockKind::Heading;
			b.level = lvl;
			b.src_line = (int)i;
			b.spans = ParseInline(rest);
			out.push_back(b);
			i++;
			continue;
		}

		// A shortcode alone on its line. Anything else containing {{< >}} is
		// left inside its paragraph.
		if ((util::StartsWith(t, "{{<") || util::StartsWith(t, "{{%")) &&
		    (util::EndsWith(t, ">}}") || util::EndsWith(t, "%}}"))) {
			Block b;
			b.kind = BlockKind::Shortcode;
			b.src_line = (int)i;
			b.text = t;
			out.push_back(b);
			i++;
			continue;
		}

		if (util::StartsWith(t, ">")) {
			Block b;
			b.kind = BlockKind::Quote;
			b.src_line = (int)i;
			std::string text;
			size_t j = i;
			for (; j < lines.size(); j++) {
				std::string q = util::Trim(lines[j]);
				if (q.empty()) break;
				if (util::StartsWith(q, ">")) q = util::Trim(q.substr(1));
				if (!text.empty()) text += " ";
				text += q;
			}
			b.spans = ParseInline(text);
			out.push_back(b);
			i = j;
			continue;
		}

		// Pipe table: a header row plus a |---|---| separator underneath.
		if (t.find('|') != std::string::npos && i + 1 < lines.size() && IsTableSeparator(lines[i + 1])) {
			Block b;
			b.kind = BlockKind::Table;
			b.src_line = (int)i;
			b.has_header = true;
			b.rows.push_back(SplitRow(lines[i]));
			size_t j = i + 2;
			for (; j < lines.size(); j++) {
				std::string r = util::Trim(lines[j]);
				if (r.empty() || r.find('|') == std::string::npos) break;
				b.rows.push_back(SplitRow(lines[j]));
			}
			out.push_back(b);
			i = j;
			continue;
		}

		bool ordered = false;
		std::string marker;
		if (ListMarker(t, &ordered, &marker)) {
			Block b;
			b.kind = BlockKind::List;
			b.src_line = (int)i;
			size_t j = i;
			for (; j < lines.size(); j++) {
				std::string lt = util::Trim(lines[j]);
				if (lt.empty()) break;
				bool ord = false;
				std::string mk;
				size_t mlen = ListMarker(lt, &ord, &mk);
				if (!mlen) {
					// A continuation line folds into the item above it.
					if (!b.items.empty()) {
						std::vector<Span> more = ParseInline(" " + lt);
						b.items.back().spans.insert(b.items.back().spans.end(), more.begin(), more.end());
						continue;
					}
					break;
				}
				ListItem it;
				it.indent = (int)(LeadingSpaces(lines[j]) / 2);
				it.ordered = ord;
				it.marker = mk;
				std::string body = lt.substr(mlen);
				if (util::StartsWith(body, "[ ] ") || util::StartsWith(body, "[x] ") || util::StartsWith(body, "[X] ")) {
					it.task = true;
					it.checked = (body[1] != ' ');
					body = body.substr(4);
				}
				it.spans = ParseInline(body);
				b.items.push_back(it);
			}
			out.push_back(b);
			i = j;
			continue;
		}

		// Raw HTML block (markup.goldmark.renderer.unsafe = true in hugo.toml,
		// so this really does reach the page).
		if (t[0] == '<') {
			Block b;
			b.kind = BlockKind::Html;
			b.src_line = (int)i;
			size_t j = i;
			for (; j < lines.size(); j++) {
				if (util::Trim(lines[j]).empty()) break;
				b.text += lines[j];
				b.text += "\n";
			}
			out.push_back(b);
			i = j;
			continue;
		}

		// Paragraph: soft-wrapped lines joined with a space, the way
		// Goldmark joins them.
		{
			Block b;
			b.kind = BlockKind::Paragraph;
			b.src_line = (int)i;
			std::string text;
			size_t j = i;
			for (; j < lines.size(); j++) {
				std::string pt = util::Trim(lines[j]);
				if (pt.empty()) break;
				bool ord = false;
				std::string mk;
				std::string dummy;
				if (util::StartsWith(pt, "```") || util::StartsWith(pt, ">") || HeadingLevel(pt, &dummy) ||
				    IsRule(pt) || ListMarker(pt, &ord, &mk))
					break;
				if (!text.empty()) text += " ";
				text += pt;
			}
			b.spans = ParseInline(text);
			out.push_back(b);
			i = (j > i) ? j : i + 1;
			continue;
		}
	}
	return out;
}

// ---- toolbar transforms ---------------------------------------------------

namespace {

void Clamp(const std::string &t, int *a, int *b) {
	if (*a < 0) *a = 0;
	if (*b < 0) *b = 0;
	if (*a > (int)t.size()) *a = (int)t.size();
	if (*b > (int)t.size()) *b = (int)t.size();
	if (*a > *b) std::swap(*a, *b);
}

} // namespace

void WrapSelection(std::string *text, int *a, int *b, const std::string &open, const std::string &close) {
	Clamp(*text, a, b);
	int oa = (int)open.size(), ob = (int)close.size();

	// Already wrapped, markers outside the selection?
	if (*a >= oa && *b + ob <= (int)text->size() && text->compare(*a - oa, oa, open) == 0 &&
	    text->compare(*b, ob, close) == 0) {
		text->erase(*b, ob);
		text->erase(*a - oa, oa);
		*a -= oa;
		*b -= oa;
		return;
	}
	// Already wrapped, markers inside the selection?
	if (*b - *a >= oa + ob && text->compare(*a, oa, open) == 0 && text->compare(*b - ob, ob, close) == 0) {
		text->erase(*b - ob, ob);
		text->erase(*a, oa);
		*b -= oa + ob;
		return;
	}
	text->insert(*b, close);
	text->insert(*a, open);
	*a += oa;
	*b += oa;
}

void PrefixLines(std::string *text, int *a, int *b, const std::string &prefix) {
	Clamp(*text, a, b);
	// Line containing *a.
	size_t start = text->rfind('\n', *a > 0 ? *a - 1 : 0);
	start = (start == std::string::npos) ? 0 : start + 1;
	if (*a == 0) start = 0;
	// Line containing *b -- but a selection ending exactly on a newline does
	// not reach into the line below it.
	int bend = *b;
	if (bend > *a && bend > 0 && (*text)[bend - 1] == '\n') bend--;
	size_t end = text->find('\n', bend);
	if (end == std::string::npos) end = text->size();

	std::vector<std::pair<size_t, size_t>> line_spans; // [begin, end) per line
	size_t p = start;
	while (p <= end) {
		size_t nl = text->find('\n', p);
		size_t e = (nl == std::string::npos || nl > end) ? end : nl;
		line_spans.push_back({p, e});
		if (nl == std::string::npos || nl >= end) break;
		p = nl + 1;
	}

	bool all_prefixed = true;
	for (auto &ls : line_spans) {
		if (ls.first == ls.second) continue; // blank line: never blocks a toggle
		if (text->compare(ls.first, prefix.size(), prefix) != 0) { all_prefixed = false; break; }
	}

	int delta = 0;
	for (auto it = line_spans.rbegin(); it != line_spans.rend(); ++it) {
		if (it->first == it->second && line_spans.size() > 1) continue;
		if (all_prefixed) {
			text->erase(it->first, prefix.size());
			delta -= (int)prefix.size();
		} else {
			text->insert(it->first, prefix);
			delta += (int)prefix.size();
		}
	}
	*a = (int)start;
	*b = (int)end + delta;
	Clamp(*text, a, b);
}

void SetHeading(std::string *text, int *a, int *b, int level) {
	Clamp(*text, a, b);
	size_t start = text->rfind('\n', *a > 0 ? *a - 1 : 0);
	start = (start == std::string::npos || *a == 0) ? 0 : start + 1;

	size_t i = start;
	while (i < text->size() && (*text)[i] == ' ') i++;
	int have = 0;
	while (i + have < text->size() && (*text)[i + have] == '#') have++;
	size_t after = i + have;
	int strip = have;
	if (have > 0 && after < text->size() && (*text)[after] == ' ') strip = have + 1;
	else if (have > 0 && (after >= text->size() || (*text)[after] == '\n')) strip = have;
	else if (have > 0) { have = 0; strip = 0; } // "#tag": not a heading at all

	std::string add = (have == level) ? "" : std::string(level, '#') + " ";
	int delta = (int)add.size() - strip;
	text->replace(i, strip, add);
	*a += delta;
	*b += delta;
	if (*a < (int)i) *a = (int)i;
	if (*b < *a) *b = *a;
	Clamp(*text, a, b);
}

void InsertText(std::string *text, int *a, int *b, const std::string &s) {
	Clamp(*text, a, b);
	text->replace(*a, *b - *a, s);
	*a = *a + (int)s.size();
	*b = *a;
}

void MakeLink(std::string *text, int *a, int *b, const std::string &url, bool image) {
	Clamp(*text, a, b);
	std::string label = text->substr(*a, *b - *a);
	if (label.empty()) label = image ? "alt text" : "link text";
	std::string s = (image ? "![" : "[") + label + "](" + url + ")";
	int start = *a;
	text->replace(*a, *b - *a, s);
	// Leave the label selected so it can be typed over straight away.
	*a = start + (image ? 2 : 1);
	*b = *a + (int)label.size();
}

} // namespace md
