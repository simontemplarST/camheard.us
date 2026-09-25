#include "fuzzy.h"

#include <cctype>

namespace fuzzy {
namespace {

char Low(char c) { return (char)tolower((unsigned char)c); }

bool IsBoundary(char c) {
	return c == '/' || c == '-' || c == '_' || c == ' ' || c == '.' || c == ':' || c == '\\';
}

} // namespace

Match Score(const std::string &hay, const std::string &needle) {
	Match m;
	if (needle.empty()) {
		m.hit = true;
		return m;
	}
	if (hay.empty()) return m;

	// Pass one, forwards: the leftmost subsequence there is. If this fails
	// there is no match at all, whatever the alignment.
	std::vector<int> fwd;
	fwd.reserve(needle.size());
	size_t h = 0;
	for (size_t n = 0; n < needle.size(); n++) {
		char want = Low(needle[n]);
		while (h < hay.size() && Low(hay[h]) != want) h++;
		if (h >= hay.size()) return m;
		fwd.push_back((int)h);
		h++;
	}

	// Pass two, backwards from where pass one ended: the same letters packed
	// as far right as they will go. "abt" against "about" comes out of pass
	// one as a-b-t spread over the whole word and out of pass two as the
	// tightest run ending at the same `t`, which is the one worth scoring.
	std::vector<int> pos(needle.size());
	int limit = fwd.back();
	for (int n = (int)needle.size() - 1; n >= 0; n--) {
		char want = Low(needle[n]);
		while (limit >= 0 && Low(hay[limit]) != want) limit--;
		pos[n] = limit;  // pass one proved a match exists at or before here
		limit--;
	}

	int score = 0;
	for (size_t i = 0; i < pos.size(); i++) {
		int p = pos[i];
		score += 2; // every matched letter is worth something
		if (p == 0) score += 20;
		else if (IsBoundary(hay[p - 1])) score += 12;
		else if (islower((unsigned char)hay[p - 1]) && isupper((unsigned char)hay[p])) score += 8;
		if (i > 0) {
			int gap = p - pos[i - 1] - 1;
			if (gap == 0) score += 14;   // runs of letters read as one word
			else score -= gap > 12 ? 12 : gap;
		}
	}
	// Front of the string beats the middle of it, and a short haystack beats
	// a long one when everything else ties -- "about" over "about-the-site".
	score -= pos[0] / 3;
	score -= (int)hay.size() / 20;
	m.hit = true;
	m.score = score;
	m.pos = pos;
	return m;
}

} // namespace fuzzy
