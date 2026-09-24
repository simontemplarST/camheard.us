// util -- string/file/path helpers shared by every other module here.
//
// Deliberately tiny and dependency-free: this GUI vendors ImGui and nothing
// else, so anything a bigger project would pull from a library (trim, split,
// slugify, "read the whole file") lives here instead.
#pragma once

#include <string>
#include <vector>

namespace util {

std::string Trim(const std::string &s);
std::string TrimRight(const std::string &s);
bool StartsWith(const std::string &s, const std::string &pre);
bool EndsWith(const std::string &s, const std::string &suf);
bool IContains(const std::string &hay, const std::string &needle);
std::string Lower(const std::string &s);
std::vector<std::string> SplitLines(const std::string &s);
std::string Join(const std::vector<std::string> &v, const std::string &sep);
std::string Replace(std::string s, const std::string &from, const std::string &to);

// "My First Post!" -> "my-first-post". Hugo's own rule closely enough: keep
// [a-z0-9], collapse everything else to single hyphens, trim them.
std::string Slugify(const std::string &s);

bool ReadFile(const std::string &path, std::string *out);
// Writes via <path>.tmp + rename so a crash mid-save can never leave a
// half-written post behind.
bool WriteFileAtomic(const std::string &path, const std::string &data);

std::string DirName(const std::string &path);
std::string BaseName(const std::string &path);
std::string StripExt(const std::string &s);
bool IsDir(const std::string &path);
bool Exists(const std::string &path);
bool MakeDirs(const std::string &path);
long long MTime(const std::string &path);
long long FileSize(const std::string &path);

// RFC3339 with local offset, the shape `hugo new` writes into archetypes.
std::string NowRFC3339();
// "2026-09-01T21:42:44-05:00" -> "2026-09-01 21:42" for table display.
std::string PrettyDate(const std::string &rfc3339);
// A stat(2) mtime -> "2026-09-24 12:49".
std::string PrettyEpoch(long long epoch);

std::string HumanSize(long long bytes);

// Fire-and-forget xdg-open; never blocks the UI thread.
void OpenURL(const std::string &url);

} // namespace util
