// toml_edit -- a line-preserving editor for hugo.toml.
//
// Deliberately NOT a TOML parser + serialiser. hugo.toml is a hand-written
// file full of comments ("# Enables PaperMod's Fuse.js client-side search",
// the commented-out homeInfoParams block, the reasons behind
// disableHLJS/noClasses). Round-tripping it through a real parser would
// throw every one of those away the first time the GUI saved. So instead
// this keeps the file as a vector of lines and rewrites exactly the one line
// a setter targets, preserving indentation and any trailing comment.
//
// Scope, honestly stated: single-line values only. A value split across
// lines (a multi-line array, a triple-quoted string) is read as whatever its
// first line holds and is never rewritten in place -- hugo.toml has none,
// and the GUI only ever writes keys it knows about.
#pragma once

#include <string>
#include <vector>

namespace tomledit {

// A key path is dotted and absolute, matching TOML's own table syntax:
// "title", "params.author", "params.profileMode.subtitle".
struct Doc {
	std::vector<std::string> lines;

	bool Load(const std::string &path);
	bool Save(const std::string &path) const;
	std::string Text() const;
	void SetText(const std::string &text);

	bool Has(const std::string &path) const;
	std::string GetRaw(const std::string &path) const;  // value as written, quotes and all
	std::string GetString(const std::string &path, const std::string &def = "") const;
	bool GetBool(const std::string &path, bool def = false) const;
	double GetNum(const std::string &path, double def = 0) const;
	std::vector<std::string> GetList(const std::string &path) const;

	void SetRaw(const std::string &path, const std::string &raw);
	void SetString(const std::string &path, const std::string &v);
	void SetBool(const std::string &path, bool v);
	void SetNum(const std::string &path, double v);
	void SetList(const std::string &path, const std::vector<std::string> &v);
	void Remove(const std::string &path);          // deletes the line outright
	void Comment(const std::string &path);         // `#`s the line out, keeping it as a hint

	// ---- arrays of tables ([[params.socialIcons]], [[menu.main]]) ----------
	// These are edited as whole lists (add/remove/reorder are the only
	// operations the UI offers), so the region holding every [[prefix]] block
	// is regenerated in one go rather than patched line by line. Values are
	// raw TOML, so a caller round-trips a string through Quote().
	struct Table {
		std::vector<std::pair<std::string, std::string>> kv;
		std::string Get(const std::string &key) const;
		std::string GetString(const std::string &key) const;
		void Set(const std::string &key, const std::string &raw);
	};
	std::vector<Table> GetTables(const std::string &prefix) const;
	void SetTables(const std::string &prefix, const std::vector<Table> &tables);
};

std::string Quote(const std::string &s);
std::string Unquote(const std::string &raw);

} // namespace tomledit
