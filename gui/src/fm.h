// fm -- Hugo front matter: parse, edit, serialise.
//
// Handles the two fences Hugo uses in this site: `+++` (TOML, what
// archetypes/default.md writes) and `---` (YAML, what most third-party
// content arrives as). Both round-trip through the same ordered key/value
// model, so the editor's form can present them identically and a YAML post
// stays YAML on save.
//
// Unknown keys are preserved verbatim in their original order -- the editor
// shows them in an "other fields" table rather than silently dropping
// anything Hugo or PaperMod understands but this GUI doesn't.
#pragma once

#include <string>
#include <vector>

namespace fm {

enum class Kind { TOML, YAML, None };

enum class Type { Str, Bool, Num, List };

struct Value {
	Type type = Type::Str;
	std::string s;
	bool b = false;
	double n = 0;
	std::vector<std::string> list;
};

struct Field {
	std::string key;
	Value v;
};

struct Doc {
	Kind kind = Kind::TOML;
	std::vector<Field> fields;
	std::string body;

	Field *Find(const std::string &key);
	const Field *Find(const std::string &key) const;

	std::string GetStr(const std::string &key, const std::string &def = "") const;
	bool GetBool(const std::string &key, bool def = false) const;
	double GetNum(const std::string &key, double def = 0) const;
	std::vector<std::string> GetList(const std::string &key) const;

	void SetStr(const std::string &key, const std::string &val);
	void SetBool(const std::string &key, bool val);
	void SetNum(const std::string &key, double val);
	void SetList(const std::string &key, const std::vector<std::string> &val);
	void Remove(const std::string &key);
	bool Has(const std::string &key) const { return Find(key) != nullptr; }
};

// Splits a whole .md file into front matter + body. A file with no fence
// parses as Kind::None with everything in `body` -- never an error, because
// Hugo accepts such a file too.
Doc Parse(const std::string &file_text);

// front matter + "\n" + body. Serialising a Kind::None doc emits the body
// alone, so an unfenced file stays unfenced unless a field is added.
std::string Serialize(const Doc &d);

// Markdown body -> word count, the way Hugo's .WordCount does it closely
// enough for the editor's status line (fences and shortcodes excluded).
int WordCount(const std::string &body);

} // namespace fm
