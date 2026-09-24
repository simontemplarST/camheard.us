// md_render -- draws the md::Block model with ImGui, in PaperMod's colours.
//
// This is the "what you see" half of the editor: the same headings, code
// blocks, quotes, tables and link styling the built site uses, painted with
// the theme's own CSS variables so the preview is a fair likeness rather
// than a generic markdown viewer. It is a likeness, though, not a second
// Goldmark -- the editor keeps a Preview in browser button next to it for
// the authoritative render.
#pragma once

#include "md.h"
#include "theme.h"

#include <functional>
#include <string>

namespace md {

struct RenderStyle {
	Paper::Palette pal;
	float base_px = 17.0f;
	// Resolves an image src the way Hugo will: returns false to draw the
	// placeholder in the "this file isn't there" colour, which is the most
	// common broken thing in a post.
	std::function<bool(const std::string &)> image_exists;
	// Called when a link is clicked. Left null, links are drawn but inert.
	std::function<void(const std::string &)> on_link;
};

// Renders into the current window at the current cursor, wrapping to the
// available content width.
void Render(const std::vector<Block> &blocks, const RenderStyle &st);

// Just the inline run -- used by Render and by the site-chrome mock in the
// Design tab (menu labels, the profile subtitle).
void RenderSpans(const std::vector<Span> &spans, const RenderStyle &st, float size_px, ImU32 color);

} // namespace md
