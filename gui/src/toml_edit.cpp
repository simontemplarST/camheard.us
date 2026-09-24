#include "toml_edit.h"

#include "util.h"

#include <cstdio>
#include <cstdlib>

namespace tomledit {
namespace {

std::string Indent(const std::string &line) {
	size_t i = 0;
	while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) i++;
	return line.substr(0, i);
}

// Index of the `#` that starts a comment, skipping any inside a quoted
// string. npos when the line has no comment.
size_t CommentStart(const std::string &s, size_t from = 0) {
	char quote = 0;
	for (size_t i = from; i < s.size(); i++) {
		char c = s[i];
		if (quote) {
			if (c == '\\' && quote == '"' && i + 1 < s.size()) { i++; continue; }
			if (c == quote) quote = 0;
		} else if (c == '"' || c == '\'') {
			quote = c;
		} else if (c == '#') {
			return i;
		}
	}
	return std::string::npos;
}

// "[params.profileMode]" -> "params.profileMode"; "" when not a header.
// `is_array` distinguishes [[x]] from [x].
std::string HeaderName(const std::string &line, bool *is_array) {
	std::string t = util::Trim(line);
	if (t.size() < 3 || t[0] != '[') return "";
	size_t cend = CommentStart(t);
	if (cend != std::string::npos) t = util::TrimRight(t.substr(0, cend));
	if (t.size() >= 4 && t[1] == '[') {
		if (!util::EndsWith(t, "]]")) return "";
		if (is_array) *is_array = true;
		return util::Trim(t.substr(2, t.size() - 4));
	}
	if (!util::EndsWith(t, "]")) return "";
	if (is_array) *is_array = false;
	return util::Trim(t.substr(1, t.size() - 2));
}

struct KeyLine {
	std::string key;
	std::string raw;      // value as written, comment stripped
	std::string comment;  // including the '#', empty when none
	bool ok = false;
};

KeyLine SplitKeyLine(const std::string &line) {
	KeyLine k;
	std::string t = util::Trim(line);
	if (t.empty() || t[0] == '#' || t[0] == '[') return k;
	// The '=' must be outside quotes: `url = "a=b"` splits at the first one,
	// which is correct, but a quoted key would not.
	size_t eq = std::string::npos;
	char quote = 0;
	for (size_t i = 0; i < t.size(); i++) {
		char c = t[i];
		if (quote) {
			if (c == '\\' && i + 1 < t.size()) { i++; continue; }
			if (c == quote) quote = 0;
		} else if (c == '"' || c == '\'') quote = c;
		else if (c == '=') { eq = i; break; }
	}
	if (eq == std::string::npos) return k;
	k.key = Unquote(util::Trim(t.substr(0, eq)));
	std::string rest = t.substr(eq + 1);
	size_t c = CommentStart(rest);
	if (c != std::string::npos) {
		k.comment = util::Trim(rest.substr(c));
		rest = rest.substr(0, c);
	}
	k.raw = util::Trim(rest);
	k.ok = !k.key.empty();
	return k;
}

void SplitPath(const std::string &path, std::string *section, std::string *key) {
	size_t p = path.find_last_of('.');
	if (p == std::string::npos) { *section = ""; *key = path; }
	else { *section = path.substr(0, p); *key = path.substr(p + 1); }
}

} // namespace

std::string Quote(const std::string &s) {
	std::string o = "\"";
	for (char c : s) {
		if (c == '"' || c == '\\') { o += '\\'; o += c; }
		else if (c == '\n') o += "\\n";
		else o += c;
	}
	o += '"';
	return o;
}

std::string Unquote(const std::string &raw) {
	std::string s = util::Trim(raw);
	if (s.size() >= 2 && s.front() == '\'' && s.back() == '\'') return s.substr(1, s.size() - 2);
	if (s.size() >= 2 && s.front() == '"' && s.back() == '"') {
		std::string inner = s.substr(1, s.size() - 2), o;
		for (size_t i = 0; i < inner.size(); i++) {
			if (inner[i] == '\\' && i + 1 < inner.size()) {
				char c = inner[++i];
				switch (c) {
				case 'n': o += '\n'; break;
				case 't': o += '\t'; break;
				case '"': o += '"'; break;
				case '\\': o += '\\'; break;
				default: o += '\\'; o += c; break;
				}
			} else o += inner[i];
		}
		return o;
	}
	return s;
}

std::string Doc::Table::Get(const std::string &key) const {
	for (const auto &p : kv)
		if (p.first == key) return p.second;
	return "";
}

std::string Doc::Table::GetString(const std::string &key) const { return Unquote(Get(key)); }

void Doc::Table::Set(const std::string &key, const std::string &raw) {
	for (auto &p : kv)
		if (p.first == key) { p.second = raw; return; }
	kv.push_back({key, raw});
}

bool Doc::Load(const std::string &path) {
	std::string text;
	if (!util::ReadFile(path, &text)) return false;
	SetText(text);
	return true;
}

void Doc::SetText(const std::string &text) { lines = util::SplitLines(text); }

std::string Doc::Text() const {
	std::string out;
	for (const auto &l : lines) { out += l; out += "\n"; }
	return out;
}

bool Doc::Save(const std::string &path) const { return util::WriteFileAtomic(path, Text()); }

// Walks the file tracking the current table header, and returns the index of
// the line holding `path`. Lines inside an array-of-tables block are skipped:
// a key under [[params.socialIcons]] must never answer a lookup of
// "params.socialIcons.name", which is a list, not a scalar.
static int FindKeyLine(const std::vector<std::string> &lines, const std::string &path) {
	std::string want_section, want_key;
	SplitPath(path, &want_section, &want_key);
	std::string cur;
	bool in_array = false;
	for (size_t i = 0; i < lines.size(); i++) {
		bool is_arr = false;
		std::string h = HeaderName(lines[i], &is_arr);
		if (!h.empty()) { cur = h; in_array = is_arr; continue; }
		if (in_array) continue;
		if (cur != want_section) continue;
		KeyLine k = SplitKeyLine(lines[i]);
		if (k.ok && k.key == want_key) return (int)i;
	}
	return -1;
}

bool Doc::Has(const std::string &path) const { return FindKeyLine(lines, path) >= 0; }

std::string Doc::GetRaw(const std::string &path) const {
	int i = FindKeyLine(lines, path);
	if (i < 0) return "";
	return SplitKeyLine(lines[i]).raw;
}

std::string Doc::GetString(const std::string &path, const std::string &def) const {
	int i = FindKeyLine(lines, path);
	if (i < 0) return def;
	return Unquote(SplitKeyLine(lines[i]).raw);
}

bool Doc::GetBool(const std::string &path, bool def) const {
	int i = FindKeyLine(lines, path);
	if (i < 0) return def;
	std::string r = util::Lower(SplitKeyLine(lines[i]).raw);
	if (r == "true") return true;
	if (r == "false") return false;
	return def;
}

double Doc::GetNum(const std::string &path, double def) const {
	int i = FindKeyLine(lines, path);
	if (i < 0) return def;
	std::string r = SplitKeyLine(lines[i]).raw;
	if (r.empty()) return def;
	char *end = nullptr;
	double v = std::strtod(r.c_str(), &end);
	if (!end || *end != '\0') return def;
	return v;
}

std::vector<std::string> Doc::GetList(const std::string &path) const {
	std::vector<std::string> out;
	int i = FindKeyLine(lines, path);
	if (i < 0) return out;
	std::string r = util::Trim(SplitKeyLine(lines[i]).raw);
	if (r.size() >= 2 && r.front() == '[' && r.back() == ']') r = r.substr(1, r.size() - 2);
	std::string cur;
	char quote = 0;
	for (size_t j = 0; j < r.size(); j++) {
		char c = r[j];
		if (quote) {
			if (c == '\\' && j + 1 < r.size()) { cur += c; cur += r[++j]; continue; }
			if (c == quote) quote = 0;
			cur += c;
		} else if (c == '"' || c == '\'') { quote = c; cur += c; }
		else if (c == ',') { std::string t = util::Trim(cur); if (!t.empty()) out.push_back(Unquote(t)); cur.clear(); }
		else cur += c;
	}
	std::string t = util::Trim(cur);
	if (!t.empty()) out.push_back(Unquote(t));
	return out;
}

void Doc::SetRaw(const std::string &path, const std::string &raw) {
	int i = FindKeyLine(lines, path);
	std::string section, key;
	SplitPath(path, &section, &key);
	if (i >= 0) {
		KeyLine k = SplitKeyLine(lines[i]);
		std::string out = Indent(lines[i]) + key + " = " + raw;
		if (!k.comment.empty()) out += "  " + k.comment;
		lines[i] = out;
		return;
	}

	// Not present. Find where the section's body ends and insert there --
	// after the last key line, so a new key lands with its siblings rather
	// than at the bottom of the file under some other table.
	int header = -1, insert_at = -1, commented_hint = -1;
	std::string cur, key_indent;
	bool in_array = false;
	for (size_t j = 0; j < lines.size(); j++) {
		bool is_arr = false;
		std::string h = HeaderName(lines[j], &is_arr);
		if (!h.empty()) {
			if (cur == section && header >= 0 && insert_at < 0) insert_at = (int)j;
			cur = h;
			in_array = is_arr;
			if (h == section && !is_arr) { header = (int)j; insert_at = -1; }
			continue;
		}
		if (cur != section || in_array) continue;
		std::string t = util::Trim(lines[j]);
		// A commented-out sample of the very key we're setting (hugo.toml
		// carries several) is the most informative place to insert.
		if (util::StartsWith(t, "#") && t.find(key) != std::string::npos && commented_hint < 0)
			commented_hint = (int)j;
		KeyLine k = SplitKeyLine(lines[j]);
		if (k.ok) {
			if (key_indent.empty()) key_indent = Indent(lines[j]);
			insert_at = (int)j + 1;
		}
	}
	if (section.empty() && header < 0) header = 0; // the root table always exists

	if (header < 0) {
		// No such table yet: append one.
		if (!lines.empty() && !util::Trim(lines.back()).empty()) lines.push_back("");
		lines.push_back("[" + section + "]");
		lines.push_back("  " + key + " = " + raw);
		return;
	}
	if (insert_at < 0) insert_at = commented_hint >= 0 ? commented_hint + 1 : header + 1;
	if (key_indent.empty()) key_indent = section.empty() ? "" : "  ";
	lines.insert(lines.begin() + insert_at, key_indent + key + " = " + raw);
}

void Doc::SetString(const std::string &path, const std::string &v) { SetRaw(path, Quote(v)); }
void Doc::SetBool(const std::string &path, bool v) { SetRaw(path, v ? "true" : "false"); }

void Doc::SetNum(const std::string &path, double v) {
	char buf[64];
	if (v == (long long)v) snprintf(buf, sizeof buf, "%lld", (long long)v);
	else snprintf(buf, sizeof buf, "%g", v);
	SetRaw(path, buf);
}

void Doc::SetList(const std::string &path, const std::vector<std::string> &v) {
	std::string raw = "[";
	for (size_t i = 0; i < v.size(); i++) {
		if (i) raw += ", ";
		raw += Quote(v[i]);
	}
	raw += "]";
	SetRaw(path, raw);
}

void Doc::Remove(const std::string &path) {
	int i = FindKeyLine(lines, path);
	if (i >= 0) lines.erase(lines.begin() + i);
}

void Doc::Comment(const std::string &path) {
	int i = FindKeyLine(lines, path);
	if (i >= 0) lines[i] = Indent(lines[i]) + "# " + util::Trim(lines[i]);
}

std::vector<Doc::Table> Doc::GetTables(const std::string &prefix) const {
	std::vector<Table> out;
	std::string cur;
	bool in_ours = false;
	for (const auto &line : lines) {
		bool is_arr = false;
		std::string h = HeaderName(line, &is_arr);
		if (!h.empty()) {
			in_ours = (is_arr && h == prefix);
			if (in_ours) out.push_back(Table{});
			cur = h;
			continue;
		}
		if (!in_ours || out.empty()) continue;
		KeyLine k = SplitKeyLine(line);
		if (k.ok) out.back().kv.push_back({k.key, k.raw});
	}
	(void)cur;
	return out;
}

void Doc::SetTables(const std::string &prefix, const std::vector<Table> &tables) {
	// Region = from the first [[prefix]] header through the last line that
	// belongs to the last such block. Anything else (comments between
	// blocks included) is inside the region and is regenerated, which is
	// why this is reserved for the list-shaped tables the UI owns.
	int first = -1, last = -1;
	std::string header_indent, key_indent;
	bool in_ours = false;
	for (size_t i = 0; i < lines.size(); i++) {
		bool is_arr = false;
		std::string h = HeaderName(lines[i], &is_arr);
		if (!h.empty()) {
			in_ours = (is_arr && h == prefix);
			if (in_ours) {
				if (first < 0) { first = (int)i; header_indent = Indent(lines[i]); }
				last = (int)i;
			}
			continue;
		}
		if (!in_ours) continue;
		if (SplitKeyLine(lines[i]).ok) {
			if (key_indent.empty()) key_indent = Indent(lines[i]);
			last = (int)i;
		}
	}

	std::vector<std::string> gen;
	if (header_indent.empty() && first < 0) {
		// Nothing there yet: match the indentation hugo.toml uses for the
		// parent table's own array blocks, i.e. two spaces per level below
		// root. Cheap heuristic, only ever used for a brand-new list.
		header_indent = prefix.find('.') != std::string::npos ? "  " : "";
	}
	if (key_indent.empty()) key_indent = header_indent + "  ";
	for (const Table &t : tables) {
		gen.push_back(header_indent + "[[" + prefix + "]]");
		for (const auto &p : t.kv) gen.push_back(key_indent + p.first + " = " + p.second);
	}

	if (first < 0) {
		if (!lines.empty() && !util::Trim(lines.back()).empty()) lines.push_back("");
		for (const auto &g : gen) lines.push_back(g);
		return;
	}
	lines.erase(lines.begin() + first, lines.begin() + last + 1);
	lines.insert(lines.begin() + first, gen.begin(), gen.end());
}

} // namespace tomledit
