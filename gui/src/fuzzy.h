// fuzzy -- subsequence matching with a score, for the command palette.
//
// The palette has to rank "abt" against every page title and every command
// name and put `about.md` at the top, so a plain substring test is not
// enough. This is the smallest thing that does that job well: a tightened
// subsequence match plus a handful of bonuses that mean the same thing a
// reader means by "that one" -- letters that start a word, letters that run
// together, a match near the front of a short path.
//
// No ImGui, no allocation beyond the positions, and covered by
// tests/core_test.cpp.
#pragma once

#include <string>
#include <vector>

namespace fuzzy {

struct Match {
	bool hit = false;
	int score = 0;
	std::vector<int> pos; // byte offsets into the haystack, ascending
};

// Case-insensitive. An empty needle matches everything with score 0, which
// is what makes the palette list everything before anything is typed.
Match Score(const std::string &haystack, const std::string &needle);

} // namespace fuzzy
