// shot -- read the framebuffer back and write a PNG.
//
// This exists so the UI can be looked at, not just compiled. `hugofe
// --screenshot DIR` drives every tab and writes one PNG per screen, which is
// the only way to catch the class of bug that fails no test and throws no
// error: a panel drawn off-screen, text over text, a control with no room.
//
// The PNG is written with stored (uncompressed) deflate blocks, so it needs
// no zlib -- a real, valid PNG any viewer opens, just a large one.
#pragma once

#include <string>

namespace shot {

// Reads the current GL framebuffer (w x h, bottom-up) and writes `path`.
bool Capture(int w, int h, const std::string &path);

} // namespace shot
