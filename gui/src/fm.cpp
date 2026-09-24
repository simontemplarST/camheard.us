#include "fm.h"

#include "util.h"

#include <cctype>
#include <cstdlib>
#include <cstdio>

namespace fm {
namespace {

// ---- scalar decoding -------------------------------------------------------

std::string Unquote(const std::string &raw) {
	std::string s = util::Trim(raw);
	if (s.size() >= 2 && ((s.front() == '"' && s.back() == '"') || (s.front() == '\'' && s.back() == '\''))) {
		char q = s.front();
		std::string inner = s.substr(1, s.size() - 2);
		if (q == '\'') return inner; // TOML literal / YAML single: no escapes
		std::string o;
		for (size_t i = 0; i < inner.size(); i++) {
			if (inner[i] == '\\' && i + 1 < inner.size()) {
				char c = inner[++i];
				switch (c) {
				case 'n': o += '\n'; break;
				case 't': o += '\t'; break;
				case 'r': o += '\r'; break;
				case '"': o += '"'; break;
				case '\\': o += '\\'; break;
				default: o += '\\'; o += c; break;
				}
			} else {
				o += inner[i];
			}
		}
		return o;
	}
	return s;
}

bool LooksNumeric(const std::string &s) {
	if (s.empty()) return false;
	char *end = nullptr;
	std::strtod(s.c_str(), &end);
	return end && *end == '\0';
}

// `[a, b, "c d"]` -> {a, b, c d}. Quote-aware so a comma inside a quoted
// item doesn't split it -- tags like "self-hosted, sort of" survive.
std::vector<std::string> ParseInlineList(const std::string &raw) {
	std::vector<std::string> out;
	std::string s = util::Trim(raw);
	if (s.size() >= 2 && s.front() == '[' && s.back() == ']') s = s.substr(1, s.size() - 2);
	std::string cur;
	char quote = 0;
	for (size_t i = 0; i < s.size(); i++) {
		char c = s[i];
		if (quote) {
			if (c == '\\' && i + 1 < s.size()) { cur += c; cur += s[++i]; continue; }
			if (c == quote) quote = 0;
			cur += c;
		} else if (c == '"' || c == '\'') {
			quote = c;
			cur += c;
		} else if (c == ',') {
			std::string t = util::Trim(cur);
			if (!t.empty()) out.push_back(Unquote(t));
			cur.clear();
		} else {
			cur += c;
		}
	}
	std::string t = util::Trim(cur);
	if (!t.empty()) out.push_back(Unquote(t));
	return out;
}

Value DecodeScalar(const std::string &raw) {
	Value v;
	std::string s = util::Trim(raw);
	if (!s.empty() && s.front() == '[') {
		v.type = Type::List;
		v.list = ParseInlineList(s);
		return v;
	}
	std::string low = util::Lower(s);
	if (low == "true" || low == "false") {
		v.type = Type::Bool;
		v.b = (low == "true");
		return v;
	}
	// A quoted value is a string even when it reads like a number: date =
	// '2026-09-01T21:42:44-05:00' must not become 2026 (strtod stops at '-',
	// so LooksNumeric already rejects it, but quoting is the real signal).
	bool quoted = s.size() >= 2 && ((s.front() == '"' && s.back() == '"') || (s.front() == '\'' && s.back() == '\''));
	if (!quoted && LooksNumeric(s)) {
		v.type = Type::Num;
		v.n = std::strtod(s.c_str(), nullptr);
		return v;
	}
	v.type = Type::Str;
	v.s = Unquote(s);
	return v;
}

std::string QuoteTOML(const std::string &s) {
	std::string o = "\"";
	for (char c : s) {
		if (c == '"' || c == '\\') { o += '\\'; o += c; }
		else if (c == '\n') o += "\\n";
		else if (c == '\t') o += "\\t";
		else o += c;
	}
	o += '"';
	return o;
}

std::string QuoteYAML(const std::string &s) { return QuoteTOML(s); }

std::string NumToString(double n) {
	char buf[64];
	if (n == (long long)n) snprintf(buf, sizeof buf, "%lld", (long long)n);
	else snprintf(buf, sizeof buf, "%g", n);
	return buf;
}

} // namespace

// ---- Doc accessors ---------------------------------------------------------

Field *Doc::Find(const std::string &key) {
	for (auto &f : fields)
		if (f.key == key) return &f;
	return nullptr;
}

const Field *Doc::Find(const std::string &key) const {
	for (auto &f : fields)
		if (f.key == key) return &f;
	return nullptr;
}

std::string Doc::GetStr(const std::string &key, const std::string &def) const {
	const Field *f = Find(key);
	if (!f) return def;
	switch (f->v.type) {
	case Type::Str: return f->v.s;
	case Type::Bool: return f->v.b ? "true" : "false";
	case Type::Num: return NumToString(f->v.n);
	case Type::List: return util::Join(f->v.list, ", ");
	}
	return def;
}

bool Doc::GetBool(const std::string &key, bool def) const {
	const Field *f = Find(key);
	if (!f) return def;
	if (f->v.type == Type::Bool) return f->v.b;
	if (f->v.type == Type::Str) return util::Lower(f->v.s) == "true";
	if (f->v.type == Type::Num) return f->v.n != 0;
	return def;
}

double Doc::GetNum(const std::string &key, double def) const {
	const Field *f = Find(key);
	if (!f) return def;
	if (f->v.type == Type::Num) return f->v.n;
	if (f->v.type == Type::Str && LooksNumeric(f->v.s)) return std::strtod(f->v.s.c_str(), nullptr);
	return def;
}

std::vector<std::string> Doc::GetList(const std::string &key) const {
	const Field *f = Find(key);
	if (!f) return {};
	if (f->v.type == Type::List) return f->v.list;
	if (f->v.type == Type::Str && !f->v.s.empty()) return {f->v.s};
	return {};
}

void Doc::SetStr(const std::string &key, const std::string &val) {
	Field *f = Find(key);
	if (!f) { fields.push_back({key, {}}); f = &fields.back(); }
	f->v = Value{};
	f->v.type = Type::Str;
	f->v.s = val;
}

void Doc::SetBool(const std::string &key, bool val) {
	Field *f = Find(key);
	if (!f) { fields.push_back({key, {}}); f = &fields.back(); }
	f->v = Value{};
	f->v.type = Type::Bool;
	f->v.b = val;
}

void Doc::SetNum(const std::string &key, double val) {
	Field *f = Find(key);
	if (!f) { fields.push_back({key, {}}); f = &fields.back(); }
	f->v = Value{};
	f->v.type = Type::Num;
	f->v.n = val;
}

void Doc::SetList(const std::string &key, const std::vector<std::string> &val) {
	Field *f = Find(key);
	if (!f) { fields.push_back({key, {}}); f = &fields.back(); }
	f->v = Value{};
	f->v.type = Type::List;
	f->v.list = val;
}

void Doc::Remove(const std::string &key) {
	for (size_t i = 0; i < fields.size(); i++)
		if (fields[i].key == key) { fields.erase(fields.begin() + i); return; }
}

// ---- parse -----------------------------------------------------------------

Doc Parse(const std::string &text) {
	Doc d;
	std::vector<std::string> lines = util::SplitLines(text);
	size_t i = 0;
	// A leading UTF-8 BOM would otherwise make the fence test fail and the
	// whole post parse as body-only.
	if (!lines.empty() && util::StartsWith(lines[0], "\xEF\xBB\xBF")) lines[0] = lines[0].substr(3);
	while (i < lines.size() && util::Trim(lines[i]).empty()) i++;
	if (i >= lines.size()) {
		d.kind = Kind::None;
		d.body = text;
		return d;
	}
	std::string fence = util::Trim(lines[i]);
	if (fence == "+++") d.kind = Kind::TOML;
	else if (fence == "---") d.kind = Kind::YAML;
	else {
		d.kind = Kind::None;
		d.body = text;
		return d;
	}

	size_t start = i + 1, end = std::string::npos;
	for (size_t j = start; j < lines.size(); j++) {
		if (util::Trim(lines[j]) == fence) { end = j; break; }
	}
	if (end == std::string::npos) {
		// Unterminated fence: treat the whole file as body rather than
		// swallowing the post's text into front matter.
		d.kind = Kind::None;
		d.body = text;
		return d;
	}

	for (size_t j = start; j < end; j++) {
		const std::string &ln = lines[j];
		std::string t = util::Trim(ln);
		if (t.empty() || t[0] == '#') continue;
		// YAML block sequence continues the previous key:
		//   tags:
		//     - linux
		if (d.kind == Kind::YAML && util::StartsWith(t, "- ") && !d.fields.empty()) {
			Field &f = d.fields.back();
			if (f.v.type != Type::List) {
				std::string prev = f.v.s;
				f.v = Value{};
				f.v.type = Type::List;
				if (!util::Trim(prev).empty()) f.v.list.push_back(prev);
			}
			f.v.list.push_back(Unquote(t.substr(2)));
			continue;
		}
		size_t eq = d.kind == Kind::TOML ? t.find('=') : t.find(':');
		if (eq == std::string::npos) continue;
		std::string key = util::Trim(t.substr(0, eq));
		std::string raw = util::Trim(t.substr(eq + 1));
		if (key.empty()) continue;
		key = Unquote(key);
		Field f;
		f.key = key;
		if (raw.empty()) {
			// `tags:` with the list on following lines -- start it empty and
			// let the block-sequence branch above fill it in.
			f.v.type = Type::List;
		} else {
			f.v = DecodeScalar(raw);
		}
		d.fields.push_back(f);
	}

	std::string body;
	for (size_t j = end + 1; j < lines.size(); j++) {
		body += lines[j];
		if (j + 1 < lines.size()) body += "\n";
	}
	// Drop exactly the blank line the fence is normally followed by; keep any
	// further ones, they may be deliberate.
	if (util::StartsWith(body, "\n")) body = body.substr(1);
	if (util::EndsWith(text, "\n") && !util::EndsWith(body, "\n")) body += "\n";
	d.body = body;
	return d;
}

// ---- serialise -------------------------------------------------------------

std::string Serialize(const Doc &d) {
	if (d.kind == Kind::None && d.fields.empty()) return d.body;

	std::string out;
	const bool toml = d.kind != Kind::YAML;
	out += toml ? "+++\n" : "---\n";
	for (const Field &f : d.fields) {
		out += f.key;
		out += toml ? " = " : ": ";
		switch (f.v.type) {
		case Type::Str: out += toml ? QuoteTOML(f.v.s) : QuoteYAML(f.v.s); break;
		case Type::Bool: out += f.v.b ? "true" : "false"; break;
		case Type::Num: out += NumToString(f.v.n); break;
		case Type::List: {
			out += "[";
			for (size_t i = 0; i < f.v.list.size(); i++) {
				if (i) out += ", ";
				out += toml ? QuoteTOML(f.v.list[i]) : QuoteYAML(f.v.list[i]);
			}
			out += "]";
			break;
		}
		}
		out += "\n";
	}
	out += toml ? "+++\n" : "---\n";
	std::string body = d.body;
	if (!body.empty() && body[0] != '\n') out += "\n";
	out += body;
	if (!out.empty() && out.back() != '\n') out += "\n";
	return out;
}

int WordCount(const std::string &body) {
	int n = 0;
	bool in_word = false, in_fence = false;
	for (const std::string &ln : util::SplitLines(body)) {
		std::string t = util::Trim(ln);
		if (util::StartsWith(t, "```")) { in_fence = !in_fence; continue; }
		if (in_fence) continue;
		in_word = false;
		for (size_t i = 0; i < ln.size(); i++) {
			unsigned char c = (unsigned char)ln[i];
			// `{{< shortcode >}}` is markup, not prose -- skip to its close.
			if (c == '{' && i + 1 < ln.size() && ln[i + 1] == '{') {
				size_t e = ln.find("}}", i);
				if (e == std::string::npos) break;
				i = e + 1;
				in_word = false;
				continue;
			}
			bool word_char = std::isalnum(c) || c == '\'' || c >= 0x80;
			if (word_char && !in_word) { n++; in_word = true; }
			else if (!word_char) in_word = false;
		}
	}
	return n;
}

} // namespace fm
