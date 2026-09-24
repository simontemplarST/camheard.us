#include "shot.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

// glReadPixels is core GL 1.0 -- declared here rather than pulled in through
// a loader, because the ImGui backend's loader is internal to that
// translation unit and this is the only GL call outside it.
extern "C" void glReadPixels(int x, int y, int width, int height, unsigned format, unsigned type, void *pixels);
#define GL_RGB_ 0x1907
#define GL_UNSIGNED_BYTE_ 0x1401

namespace shot {
namespace {

uint32_t Crc32(const uint8_t *data, size_t len, uint32_t crc = 0xFFFFFFFFu) {
	static uint32_t table[256];
	static bool init = false;
	if (!init) {
		for (uint32_t i = 0; i < 256; i++) {
			uint32_t c = i;
			for (int k = 0; k < 8; k++) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
			table[i] = c;
		}
		init = true;
	}
	for (size_t i = 0; i < len; i++) crc = table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
	return crc;
}

uint32_t Adler32(const uint8_t *data, size_t len) {
	uint32_t a = 1, b = 0;
	for (size_t i = 0; i < len; i++) {
		a = (a + data[i]) % 65521;
		b = (b + a) % 65521;
	}
	return (b << 16) | a;
}

void PutBE32(std::vector<uint8_t> &v, uint32_t x) {
	v.push_back((uint8_t)(x >> 24));
	v.push_back((uint8_t)(x >> 16));
	v.push_back((uint8_t)(x >> 8));
	v.push_back((uint8_t)x);
}

void Chunk(std::vector<uint8_t> &out, const char type[4], const std::vector<uint8_t> &data) {
	PutBE32(out, (uint32_t)data.size());
	size_t start = out.size();
	out.insert(out.end(), type, type + 4);
	out.insert(out.end(), data.begin(), data.end());
	uint32_t crc = Crc32(out.data() + start, out.size() - start) ^ 0xFFFFFFFFu;
	PutBE32(out, crc);
}

} // namespace

bool Capture(int w, int h, const std::string &path) {
	if (w <= 0 || h <= 0) return false;
	std::vector<uint8_t> pix((size_t)w * h * 3);
	glReadPixels(0, 0, w, h, GL_RGB_, GL_UNSIGNED_BYTE_, pix.data());

	// PNG rows run top-down and each carries a filter byte; GL hands back
	// bottom-up, so this flips while it packs.
	std::vector<uint8_t> raw;
	raw.reserve((size_t)h * (w * 3 + 1));
	for (int y = h - 1; y >= 0; y--) {
		raw.push_back(0); // filter: none
		const uint8_t *row = pix.data() + (size_t)y * w * 3;
		raw.insert(raw.end(), row, row + (size_t)w * 3);
	}

	// zlib stream with stored blocks: no compressor needed, still valid.
	std::vector<uint8_t> z;
	z.push_back(0x78);
	z.push_back(0x01);
	const size_t kMax = 65535;
	for (size_t off = 0; off < raw.size(); off += kMax) {
		size_t n = raw.size() - off < kMax ? raw.size() - off : kMax;
		bool last = (off + n >= raw.size());
		z.push_back(last ? 1 : 0);
		z.push_back((uint8_t)(n & 0xFF));
		z.push_back((uint8_t)(n >> 8));
		z.push_back((uint8_t)(~n & 0xFF));
		z.push_back((uint8_t)((~n >> 8) & 0xFF));
		z.insert(z.end(), raw.begin() + (long)off, raw.begin() + (long)(off + n));
	}
	PutBE32(z, Adler32(raw.data(), raw.size()));

	std::vector<uint8_t> png = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
	std::vector<uint8_t> ihdr;
	PutBE32(ihdr, (uint32_t)w);
	PutBE32(ihdr, (uint32_t)h);
	ihdr.push_back(8); // bit depth
	ihdr.push_back(2); // colour type: truecolour
	ihdr.push_back(0);
	ihdr.push_back(0);
	ihdr.push_back(0);
	Chunk(png, "IHDR", ihdr);
	Chunk(png, "IDAT", z);
	Chunk(png, "IEND", {});

	FILE *f = fopen(path.c_str(), "wb");
	if (!f) return false;
	bool ok = fwrite(png.data(), 1, png.size(), f) == png.size();
	fclose(f);
	return ok;
}

} // namespace shot
