// RPCS3 PS5 - PS3 games on the PS5's own home screen, and deleting games (tiles.hpp).
// SPDX-License-Identifier: GPL-3.0-or-later

#include "tiles.hpp"
#include "core.hpp"
#include "requests.hpp"

#include "stb_image.h" // implemented in base/VulkanglTFModel.cpp

#include <arpa/inet.h>
#include <dirent.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <functional>
#include <mutex>
#include <sstream>
#include <thread>
#include <vector>

#ifndef PS5_APP_ROOT
#define PS5_APP_ROOT "/app0"
#endif

// zlib, linked in with RADV (the PNG encoder below)
extern "C" {
int compress2(unsigned char *dest, unsigned long *dest_len, const unsigned char *source, unsigned long source_len, int level);
unsigned long compressBound(unsigned long source_len);
unsigned long crc32(unsigned long crc, const unsigned char *buf, unsigned int len);
}

namespace rpcs3ps5
{

namespace
{

constexpr int kHelperPorts[] = {9078, 9079}; // 9079: when 9078 is held by a stuck helper
constexpr int kLoaderPort = 9021;
constexpr std::int64_t kHelperVersion = 9;
constexpr std::uint32_t kMagic = 0x4f493352u; // "R3IO"
constexpr std::size_t kChunk = 1u << 20;

std::atomic<bool> g_busy{false};
std::atomic<bool> g_rescan{false};
std::mutex g_list_mutex;

std::string tiles_file()
{
	return Library::root() + "/ps5-tiles.txt";
}

// ---- the write helper (tools/iohelper) ----

struct __attribute__((packed)) HelperRequest
{
	std::uint32_t magic;
	std::uint32_t path_len;
	std::uint64_t inode;
	std::uint64_t offset;
	std::uint64_t size;
};

bool send_all(int s, const void *data, std::size_t size)
{
	const char *p = static_cast<const char *>(data);
	while (size) {
		const ssize_t n = ::send(s, p, size, 0);
		if (n <= 0)
			return false;
		p += n;
		size -= static_cast<std::size_t>(n);
	}
	return true;
}

bool recv_all(int s, void *data, std::size_t size)
{
	char *p = static_cast<char *>(data);
	while (size) {
		const ssize_t n = ::recv(s, p, size, 0);
		if (n <= 0)
			return false;
		p += n;
		size -= static_cast<std::size_t>(n);
	}
	return true;
}

int connect_local(int port)
{
	const int s = ::socket(AF_INET, SOCK_STREAM, 0);
	if (s < 0)
		return -1;
	sockaddr_in a{};
	a.sin_family = AF_INET;
	a.sin_port = htons(static_cast<std::uint16_t>(port));
	a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	if (::connect(s, reinterpret_cast<sockaddr *>(&a), sizeof(a)) != 0) {
		::close(s);
		return -1;
	}
	const int one = 1;
	::setsockopt(s, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
	// a helper stuck in a system call accepts but never answers
	timeval timeout{8, 0};
	::setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
	return s;
}

class Helper
{
public:
	~Helper()
	{
		if (socket_ >= 0)
			::close(socket_);
	}

	// Connected to a helper of this version, starting (or replacing) it when needed
	bool ready()
	{
		if (socket_ >= 0)
			return true;
		for (int attempt = 0; attempt < 2; ++attempt) {
			bool any = false;
			for (const int port : kHelperPorts) {
				socket_ = connect_local(port);
				if (socket_ < 0)
					continue;
				any = true;
				const std::int64_t version = call("\x04version", 0, nullptr, 0);
				if (version == kHelperVersion)
					return true;
				trace("tiles: helper on :%d answered %lld", port, static_cast<long long>(version));
				if (version != INT64_MIN && socket_ >= 0)
					call("\x01quit", 0, nullptr, 0); // an older one: replaced below
				drop();
			}
			(void)any;
			if (!start())
				return false;
			for (int i = 0; i < 50; ++i) {
				std::this_thread::sleep_for(std::chrono::milliseconds(100));
				for (const int port : kHelperPorts) {
					socket_ = connect_local(port);
					if (socket_ >= 0 && call("\x04version", 0, nullptr, 0) == kHelperVersion)
						return true;
					drop();
				}
			}
		}
		return false;
	}

	// One request; the helper's answer (bytes written, 0, or -errno); INT64_MIN when the link failed
	std::int64_t call(const std::string &path, std::uint64_t offset, const void *data, std::size_t size)
	{
		if (socket_ < 0)
			return INT64_MIN;
		const HelperRequest q{kMagic, static_cast<std::uint32_t>(path.size()), 0, offset, size};
		std::int64_t result = 0;
		if (!send_all(socket_, &q, sizeof(q)) || !send_all(socket_, path.data(), path.size()) ||
		    (size && !send_all(socket_, data, size)) || !recv_all(socket_, &result, sizeof(result))) {
			drop();
			return INT64_MIN;
		}
		return result;
	}

	// A whole file into a tile folder ("PPSA8nnnn/sce_sys/icon0.png"), in 1 MiB requests
	bool write_tile_file(const std::string &relative, const std::vector<std::uint8_t> &data)
	{
		std::size_t done = 0;
		do {
			const std::size_t n = std::min(kChunk, data.size() - done);
			const std::int64_t r = call("\x02tile/" + relative, done, data.data() + done, n);
			if (r != static_cast<std::int64_t>(n)) {
				trace("tiles: writing %s failed (%lld)", relative.c_str(), static_cast<long long>(r));
				return false;
			}
			done += n;
		} while (done < data.size());
		return true;
	}

private:
	void drop()
	{
		if (socket_ >= 0)
			::close(socket_);
		socket_ = -1;
	}

	// The helper's ELF to the ELF loader (as the game-install write path does)
	bool start()
	{
		std::vector<std::uint8_t> elf;
		std::ifstream in(PS5_APP_ROOT "/rpcs3ps5-io.elf", std::ios::binary);
		elf.assign(std::istreambuf_iterator<char>(in), {});
		const int loader = elf.empty() ? -1 : connect_local(kLoaderPort);
		if (loader < 0) {
			trace("tiles: no write helper and no ELF loader on :%d", kLoaderPort);
			return false;
		}
		send_all(loader, elf.data(), elf.size());
		::close(loader);
		return true;
	}

	int socket_ = -1;
};

// GameTDB's high-resolution box art for a serial, its region first. The helper downloads it (the title's
// sandbox has no network) into <root>/covers/<serial>.jpg, which the tile and the launch screen use.
bool download_cover(Helper &helper, const std::string &serial, std::vector<std::uint8_t> &jpeg)
{
	if (serial.size() != 9)
		return false;
	std::vector<std::string> regions;
	switch (serial[2]) {
	case 'U': regions = {"US", "EN"}; break;
	case 'J': regions = {"JA", "US", "EN"}; break;
	case 'A': case 'H': case 'K': regions = {"ZH", "KO", "JA", "US", "EN"}; break;
	default: regions = {"EN", "FR", "DE", "ES", "IT", "NL", "PT", "RU", "US"}; break;
	}
	for (const std::string &region : regions) {
		const std::string path = "/ps3/coverHQ/" + region + "/" + serial + ".jpg";
		const std::int64_t r = helper.call("\x06get/" + serial + ".jpg|" + path, 0, nullptr, 0);
		trace("tiles: http://art.gametdb.com%s: %lld", path.c_str(), static_cast<long long>(r));
		if (r > 1000) {
			std::ifstream in(Library::root() + "/covers/" + serial + ".jpg", std::ios::binary);
			jpeg.assign(std::istreambuf_iterator<char>(in), {});
			return !jpeg.empty();
		}
		if (r != -404)
			return false; // offline, or GameTDB unreachable
	}
	return false;
}

// ---- images ----

struct Image
{
	int w = 0, h = 0;
	std::vector<float> px; // RGBA, 0..1

	bool decode(const std::vector<std::uint8_t> &file)
	{
		int channels = 0;
		stbi_uc *rgba = file.empty() ? nullptr
		                             : stbi_load_from_memory(file.data(), static_cast<int>(file.size()), &w, &h, &channels, 4);
		if (!rgba)
			return false;
		px.resize(static_cast<std::size_t>(w) * h * 4);
		for (std::size_t i = 0; i < px.size(); ++i)
			px[i] = rgba[i] / 255.0f;
		stbi_image_free(rgba);
		return true;
	}
	bool empty() const
	{
		return px.empty();
	}
	float *at(int x, int y)
	{
		return &px[(static_cast<std::size_t>(y) * w + x) * 4];
	}
	const float *at(int x, int y) const
	{
		return &px[(static_cast<std::size_t>(y) * w + x) * 4];
	}
};

Image blank(int w, int h)
{
	Image out;
	out.w = w;
	out.h = h;
	out.px.assign(static_cast<std::size_t>(w) * h * 4, 0.0f);
	for (std::size_t i = 3; i < out.px.size(); i += 4)
		out.px[i] = 1.0f;
	return out;
}

// The source rectangle (sx, sy, sw, sh) scaled to dw x dh: area averages when shrinking, bilinear when growing
Image resample(const Image &src, float sx, float sy, float sw, float sh, int dw, int dh)
{
	Image out = blank(dw, dh);
	const float fx = sw / dw, fy = sh / dh;
	for (int y = 0; y < dh; ++y) {
		for (int x = 0; x < dw; ++x) {
			float acc[4] = {};
			if (fx > 1.0f || fy > 1.0f) {
				const int x0 = std::clamp(static_cast<int>(sx + x * fx), 0, src.w - 1);
				const int x1 = std::clamp(static_cast<int>(std::ceil(sx + (x + 1) * fx)), x0 + 1, src.w);
				const int y0 = std::clamp(static_cast<int>(sy + y * fy), 0, src.h - 1);
				const int y1 = std::clamp(static_cast<int>(std::ceil(sy + (y + 1) * fy)), y0 + 1, src.h);
				for (int yy = y0; yy < y1; ++yy)
					for (int xx = x0; xx < x1; ++xx)
						for (int c = 0; c < 4; ++c)
							acc[c] += src.at(xx, yy)[c];
				const float n = static_cast<float>((x1 - x0) * (y1 - y0));
				for (float &a : acc)
					a /= n;
			} else {
				const float u = std::clamp(sx + (x + 0.5f) * fx - 0.5f, 0.0f, static_cast<float>(src.w - 1));
				const float v = std::clamp(sy + (y + 0.5f) * fy - 0.5f, 0.0f, static_cast<float>(src.h - 1));
				const int x0 = static_cast<int>(u), y0 = static_cast<int>(v);
				const int x1 = std::min(x0 + 1, src.w - 1), y1 = std::min(y0 + 1, src.h - 1);
				const float tx = u - x0, ty = v - y0;
				for (int c = 0; c < 4; ++c) {
					const float top = src.at(x0, y0)[c] * (1 - tx) + src.at(x1, y0)[c] * tx;
					const float bottom = src.at(x0, y1)[c] * (1 - tx) + src.at(x1, y1)[c] * tx;
					acc[c] = top * (1 - ty) + bottom * ty;
				}
			}
			std::copy(acc, acc + 4, out.at(x, y));
		}
	}
	return out;
}

// Scaled and centre-cropped to fill w x h
Image fill(const Image &src, int w, int h)
{
	const float scale = std::max(static_cast<float>(w) / src.w, static_cast<float>(h) / src.h);
	const float sw = w / scale, sh = h / scale;
	return resample(src, (src.w - sw) * 0.5f, (src.h - sh) * 0.5f, sw, sh, w, h);
}

// Scaled to fit inside w x h
Image fit(const Image &src, int w, int h)
{
	const float scale = std::min(static_cast<float>(w) / src.w, static_cast<float>(h) / src.h);
	return resample(src, 0, 0, static_cast<float>(src.w), static_cast<float>(src.h),
	                std::max(1, static_cast<int>(std::lround(src.w * scale))), std::max(1, static_cast<int>(std::lround(src.h * scale))));
}

// A soft, darkened copy for behind the art: shrunk to a few pixels and grown back
Image backdrop(const Image &src, int w, int h, float brightness)
{
	const Image tiny = fill(src, std::max(4, w / 40), std::max(4, h / 40));
	Image out = resample(tiny, 0, 0, static_cast<float>(tiny.w), static_cast<float>(tiny.h), w, h);
	for (std::size_t i = 0; i < out.px.size(); ++i)
		if (i % 4 != 3)
			out.px[i] *= brightness;
	return out;
}

void place(Image &canvas, const Image &art, int left, int top)
{
	for (int y = 0; y < art.h; ++y) {
		for (int x = 0; x < art.w; ++x) {
			const int cx = left + x, cy = top + y;
			if (cx < 0 || cy < 0 || cx >= canvas.w || cy >= canvas.h)
				continue;
			const float *s = art.at(x, y);
			float *d = canvas.at(cx, cy);
			for (int c = 0; c < 3; ++c)
				d[c] = s[c] * s[3] + d[c] * (1 - s[3]);
		}
	}
}

void put_u32(std::vector<std::uint8_t> &out, std::uint32_t v)
{
	for (int i = 3; i >= 0; --i)
		out.push_back(static_cast<std::uint8_t>(v >> (i * 8)));
}

void chunk(std::vector<std::uint8_t> &out, const char *type, const std::vector<std::uint8_t> &data)
{
	put_u32(out, static_cast<std::uint32_t>(data.size()));
	const std::size_t start = out.size();
	out.insert(out.end(), type, type + 4);
	out.insert(out.end(), data.begin(), data.end());
	put_u32(out, static_cast<std::uint32_t>(crc32(0, out.data() + start, static_cast<unsigned>(out.size() - start))));
}

// RGB PNG, each row "Sub"-filtered
std::vector<std::uint8_t> encode_png(const Image &image)
{
	const std::size_t stride = static_cast<std::size_t>(image.w) * 3 + 1;
	std::vector<std::uint8_t> raw(stride * image.h);
	for (int y = 0; y < image.h; ++y) {
		std::uint8_t *row = &raw[stride * y];
		row[0] = 1; // Sub
		std::uint8_t previous[3] = {};
		for (int x = 0; x < image.w; ++x) {
			for (int c = 0; c < 3; ++c) {
				const std::uint8_t v = static_cast<std::uint8_t>(std::lround(std::clamp(image.at(x, y)[c], 0.0f, 1.0f) * 255.0f));
				row[1 + x * 3 + c] = static_cast<std::uint8_t>(v - previous[c]);
				previous[c] = v;
			}
		}
	}
	unsigned long packed_size = compressBound(raw.size());
	std::vector<std::uint8_t> packed(packed_size);
	if (compress2(packed.data(), &packed_size, raw.data(), raw.size(), 6) != 0)
		return {};
	packed.resize(packed_size);

	std::vector<std::uint8_t> png = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'};
	std::vector<std::uint8_t> header;
	put_u32(header, static_cast<std::uint32_t>(image.w));
	put_u32(header, static_cast<std::uint32_t>(image.h));
	header.insert(header.end(), {8, 2, 0, 0, 0}); // 8-bit RGB
	chunk(png, "IHDR", header);
	chunk(png, "IDAT", packed);
	chunk(png, "IEND", {});
	return png;
}

// ---- DDS (BC7): the PS5 home screen draws a game's background (pic0) and icon from these ----

// A 128-bit BC7 block in mode 6 (one subset, RGBA endpoints of 7 bits + a p-bit each, 4-bit indices)
struct Bits
{
	std::uint8_t bytes[16] = {};
	int at = 0;
	void put(std::uint32_t value, int count)
	{
		for (int i = 0; i < count; ++i, ++at)
			if (value >> i & 1)
				bytes[at >> 3] |= static_cast<std::uint8_t>(1u << (at & 7));
	}
};

void bc7_block(const Image &image, int bx, int by, std::uint8_t *out)
{
	static const int kWeights[16] = {0, 4, 9, 13, 17, 21, 26, 30, 34, 38, 43, 47, 51, 55, 60, 64};
	float px[16][4];
	float mean[4] = {};
	for (int i = 0; i < 16; ++i) {
		const int x = std::min(bx * 4 + i % 4, image.w - 1), y = std::min(by * 4 + i / 4, image.h - 1);
		for (int c = 0; c < 4; ++c) {
			px[i][c] = std::clamp(image.at(x, y)[c], 0.0f, 1.0f) * 255.0f;
			mean[c] += px[i][c] / 16.0f;
		}
	}
	// The main direction of the colours (a few power iterations), and the extremes along it
	float axis[4] = {1, 1, 1, 0};
	for (int iteration = 0; iteration < 4; ++iteration) {
		float next[4] = {};
		for (int i = 0; i < 16; ++i) {
			float d[4], dot = 0;
			for (int c = 0; c < 4; ++c) {
				d[c] = px[i][c] - mean[c];
				dot += d[c] * axis[c];
			}
			for (int c = 0; c < 4; ++c)
				next[c] += d[c] * dot;
		}
		const float length = std::sqrt(next[0] * next[0] + next[1] * next[1] + next[2] * next[2] + next[3] * next[3]);
		if (length < 1e-6f)
			break;
		for (int c = 0; c < 4; ++c)
			axis[c] = next[c] / length;
	}
	float lo = 1e9f, hi = -1e9f;
	for (int i = 0; i < 16; ++i) {
		float t = 0;
		for (int c = 0; c < 4; ++c)
			t += (px[i][c] - mean[c]) * axis[c];
		lo = std::min(lo, t);
		hi = std::max(hi, t);
	}
	int q[2][4], p[2];
	for (int e = 0; e < 2; ++e) {
		const float t = e ? hi : lo;
		float target[4];
		for (int c = 0; c < 4; ++c)
			target[c] = std::clamp(mean[c] + axis[c] * t, 0.0f, 255.0f);
		// the p-bit (shared by the endpoint's four channels) that fits best
		float best = 1e9f;
		for (int bit = 0; bit < 2; ++bit) {
			float error = 0;
			int candidate[4];
			for (int c = 0; c < 4; ++c) {
				candidate[c] = std::clamp(static_cast<int>(std::lround((target[c] - bit) / 2.0f)), 0, 127);
				const float v = static_cast<float>(candidate[c] * 2 + bit);
				error += (v - target[c]) * (v - target[c]);
			}
			if (error < best) {
				best = error;
				p[e] = bit;
				std::copy(candidate, candidate + 4, q[e]);
			}
		}
	}
	int palette[16][4];
	for (int k = 0; k < 16; ++k)
		for (int c = 0; c < 4; ++c) {
			const int e0 = q[0][c] * 2 + p[0], e1 = q[1][c] * 2 + p[1];
			palette[k][c] = ((64 - kWeights[k]) * e0 + kWeights[k] * e1 + 32) >> 6;
		}
	int index[16];
	for (int i = 0; i < 16; ++i) {
		float best = 1e18f;
		for (int k = 0; k < 16; ++k) {
			float error = 0;
			for (int c = 0; c < 4; ++c) {
				const float d = px[i][c] - static_cast<float>(palette[k][c]);
				error += d * d;
			}
			if (error < best) {
				best = error;
				index[i] = k;
			}
		}
	}
	// The first index is stored in 3 bits: its top bit must be 0, so swap the endpoints when it is not
	if (index[0] >= 8) {
		std::swap(q[0], q[1]);
		std::swap(p[0], p[1]);
		for (int &i : index)
			i = 15 - i;
	}
	Bits bits;
	bits.put(1u << 6, 7); // mode 6
	for (int c = 0; c < 4; ++c) {
		bits.put(static_cast<std::uint32_t>(q[0][c]), 7);
		bits.put(static_cast<std::uint32_t>(q[1][c]), 7);
	}
	bits.put(static_cast<std::uint32_t>(p[0]), 1);
	bits.put(static_cast<std::uint32_t>(p[1]), 1);
	bits.put(static_cast<std::uint32_t>(index[0]), 3);
	for (int i = 1; i < 16; ++i)
		bits.put(static_cast<std::uint32_t>(index[i]), 4);
	std::memcpy(out, bits.bytes, 16);
}

void put_le32(std::vector<std::uint8_t> &out, std::size_t at, std::uint32_t v)
{
	for (int i = 0; i < 4; ++i)
		out[at + i] = static_cast<std::uint8_t>(v >> (i * 8));
}

// A BC7 DDS with the DX10 header, laid out as the PS5's own games have theirs (sce_sys/pic0.dds)
std::vector<std::uint8_t> encode_dds(const Image &image)
{
	const int blocks_x = (image.w + 3) / 4, blocks_y = (image.h + 3) / 4;
	const std::size_t data = static_cast<std::size_t>(blocks_x) * blocks_y * 16;
	std::vector<std::uint8_t> dds(148 + data, 0);
	std::memcpy(dds.data(), "DDS ", 4);
	put_le32(dds, 4, 124);
	put_le32(dds, 8, 0x000A1007); // caps, height, width, pixel format, linear size
	put_le32(dds, 12, static_cast<std::uint32_t>(image.h));
	put_le32(dds, 16, static_cast<std::uint32_t>(image.w));
	put_le32(dds, 20, static_cast<std::uint32_t>(data));
	put_le32(dds, 28, 1);  // one mip level
	put_le32(dds, 76, 32); // pixel format: a FourCC, "DX10"
	put_le32(dds, 80, 4);
	std::memcpy(dds.data() + 84, "DX10", 4);
	put_le32(dds, 108, 0x1000); // a texture
	put_le32(dds, 128, 98);     // DXGI_FORMAT_BC7_UNORM
	put_le32(dds, 132, 3);      // 2D
	put_le32(dds, 140, 1);      // array size
	for (int by = 0; by < blocks_y; ++by)
		for (int bx = 0; bx < blocks_x; ++bx)
			bc7_block(image, bx, by, &dds[148 + (static_cast<std::size_t>(by) * blocks_x + bx) * 16]);
	return dds;
}

Image solid(int w, int h, float r, float g, float b, float a)
{
	Image out = blank(w, h);
	for (std::size_t i = 0; i < out.px.size(); i += 4) {
		out.px[i] = r;
		out.px[i + 1] = g;
		out.px[i + 2] = b;
		out.px[i + 3] = a;
	}
	return out;
}

// Only the artwork of a PS3 box cover: without the top banner (the "PS3 / PlayStation Network" strip, the top
// 9%) and the bottom strip where the age rating (bottom left) and the publisher's logo (bottom right) sit
Image cover_art(const Image &cover)
{
	const float top = cover.h * 0.09f, bottom = cover.h * 0.18f;
	const float h = cover.h - top - bottom;
	return resample(cover, 0, top, static_cast<float>(cover.w), h, cover.w, static_cast<int>(h));
}

// The tile (512x512): the box art (its artwork only) filling it, or the game's ICON0 on its PIC1
Image make_icon(const Image &cover, const Image &icon0, const Image &pic1)
{
	constexpr int kSize = 512;
	if (!cover.empty())
		return fill(cover_art(cover), kSize, kSize);
	Image canvas = backdrop(pic1.empty() ? icon0 : pic1, kSize, kSize, 0.55f);
	if (!icon0.empty()) {
		const Image front = fit(icon0, kSize - 24, kSize - 24);
		place(canvas, front, (kSize - front.w) / 2, (kSize - front.h) / 2);
	}
	return canvas;
}

// The background (3840x2160): the game's PIC1, else a soft copy of the box art with the cover on it
Image make_background(const Image &cover, const Image &icon0, const Image &pic1)
{
	constexpr int kW = 3840, kH = 2160;
	if (!pic1.empty())
		return fill(pic1, kW, kH);
	const Image source = cover.empty() ? icon0 : cover_art(cover);
	Image canvas = backdrop(source, kW, kH, 0.45f);
	const Image front = fit(source, kW * 2 / 5, kH * 4 / 5);
	place(canvas, front, (kW - front.w) / 2, (kH - front.h) / 2);
	return canvas;
}

std::string json_escape(const std::string &text)
{
	std::string out;
	for (const char c : text) {
		if (c == '"' || c == '\\')
			out += '\\';
		if (static_cast<unsigned char>(c) < 0x20)
			continue;
		out += c;
	}
	return out;
}

std::string param_json(const std::string &id, const std::string &name)
{
	std::ostringstream j;
	j << "{\n"
	  << "  \"ageLevel\": {\"default\": 0},\n"
	  << "  \"applicationCategoryType\": 0,\n"
	  << "  \"applicationDrmType\": \"free\",\n"
	  << "  \"attribute\": 0,\n"
	  << "  \"attribute2\": 0,\n"
	  << "  \"attribute3\": 524352,\n"
	  << "  \"conceptId\": \"" << id.substr(4) << "\",\n"
	  << "  \"contentBadgeType\": 1,\n"
	  << "  \"contentId\": \"UP9000-" << id << "_00-RPCS3TILE0000000\",\n"
	  << "  \"contentVersion\": \"01.000.000\",\n"
	  << "  \"downloadDataSize\": 0,\n"
	  << "  \"gameIntent\": {\"permittedIntents\": [{\"intentType\": \"launchActivity\"}]},\n"
	  << "  \"localizedParameters\": {\"defaultLanguage\": \"en-US\", \"en-US\": {\"titleName\": \"" << json_escape(name) << "\"}},\n"
	  << "  \"masterVersion\": \"01.00\",\n"
	  << "  \"pubtools\": {\"creationDate\": \"2026-10-07 00:00:00\", \"loudnessSnd0\": \"-28.00\", \"toolVersion\": \"2.00\"},\n"
	  << "  \"requiredSystemSoftwareVersion\": \"0x0000000000000000\",\n"
	  << "  \"sdkVersion\": \"0x0000000000000000\",\n"
	  << "  \"titleId\": \"" << id << "\",\n"
	  << "  \"versionFileUri\": \"\"\n"
	  << "}\n";
	return j.str();
}

std::vector<std::uint8_t> bytes(const std::string &text)
{
	return {text.begin(), text.end()};
}

bool read_whole(const std::string &path, std::vector<std::uint8_t> &data)
{
	std::ifstream in(path, std::ios::binary);
	if (!in)
		return false;
	data.assign(std::istreambuf_iterator<char>(in), {});
	return !data.empty();
}

// ---- the list of games with a tile ----

// One line per tile: "<tile title ID> <key>", the key being the game's serial (or, for a game without a
// readable one, its path). Older lines were "<serial> <tile ID>".
struct TileEntry
{
	std::string id, key;
};

std::vector<TileEntry> read_tiles()
{
	std::vector<TileEntry> entries;
	std::ifstream in(tiles_file());
	for (std::string line; std::getline(in, line);) {
		if (line.size() < 11 || line[9] != ' ')
			continue;
		if (line.compare(0, 5, "PPSA8") == 0)
			entries.push_back({line.substr(0, 9), line.substr(10)});
		else
			entries.push_back({line.substr(10, 9), line.substr(0, 9)});
	}
	return entries;
}

void write_tiles(const std::vector<TileEntry> &entries)
{
	std::ofstream out(tiles_file(), std::ios::trunc);
	for (const TileEntry &e : entries)
		out << e.id << ' ' << e.key << '\n';
}

std::string tile_key(const Game &game)
{
	return game.title_id.size() == 9 && game.title_id != "????00000" ? game.title_id : game.boot;
}

// The tile ID a game has, or a free one for it: PPSA8 + four digits from the key's crc32, the next free
// number when another game's tile already has it
std::string assigned_tile_id(const Game &game, bool *existing = nullptr)
{
	const std::string key = tile_key(game);
	const std::vector<TileEntry> entries = read_tiles();
	for (const TileEntry &e : entries)
		if (e.key == key) {
			if (existing)
				*existing = true;
			return e.id;
		}
	if (existing)
		*existing = false;
	unsigned number = static_cast<unsigned>(crc32(0, reinterpret_cast<const unsigned char *>(key.data()), static_cast<unsigned>(key.size())) % 10000);
	for (int tries = 0; tries < 10000; ++tries, number = (number + 1) % 10000) {
		char id[16];
		std::snprintf(id, sizeof(id), "PPSA8%04u", number);
		if (std::none_of(entries.begin(), entries.end(), [&](const TileEntry &e) { return e.id == id; }))
			return id;
	}
	return "PPSA80000";
}

void remember_tile(const Game &game, const std::string &id, bool present)
{
	std::lock_guard lock(g_list_mutex);
	std::vector<TileEntry> entries = read_tiles();
	const std::string key = tile_key(game);
	std::erase_if(entries, [&](const TileEntry &e) { return e.key == key; });
	if (present)
		entries.push_back({id, key});
	write_tiles(entries);
}

// ---- the jobs ----

void add_tile(const Game &game)
{
	std::string id;
	{
		std::lock_guard lock(g_list_mutex);
		id = assigned_tile_id(game);
	}
	set_status("Adding " + game.title + " to the PS5 home screen: getting the box art...");
	Helper helper;
	if (!helper.ready()) {
		set_status("The write helper did not start (is the ELF loader running?): " + game.title + " not added");
		return;
	}
	std::vector<std::uint8_t> file;
	Image cover, icon0, pic1;
	if (download_cover(helper, game.title_id, file) && !cover.decode(file))
		trace("tiles: the cover of %s did not decode", game.title_id.c_str());
	if (game_file(game, "ICON0.PNG", file))
		icon0.decode(file);
	if (game_file(game, "PIC1.PNG", file))
		pic1.decode(file);
	if (cover.empty() && icon0.empty() && pic1.empty()) {
		set_status("No artwork for " + game.title + " (no box art online, no ICON0.PNG): not added");
		return;
	}
	trace("tiles: %s -> %s: cover %dx%d, ICON0 %dx%d, PIC1 %dx%d", game.title_id.c_str(), id.c_str(), cover.w, cover.h,
	      icon0.w, icon0.h, pic1.w, pic1.h);

	set_status("Adding " + game.title + " to the PS5 home screen: making the icon and background...");
	const Image icon = make_icon(cover, icon0, pic1);
	const Image background = make_background(cover, icon0, pic1);
	const std::vector<std::uint8_t> icon_png = encode_png(icon);
	const std::vector<std::uint8_t> background_png = encode_png(background);
	// The home screen's own forms: the background behind a focused tile is pic0.dds; pic1 black and pic2 (a
	// logo layer) clear, as a PS5 game has them
	const std::vector<std::uint8_t> icon_dds = encode_dds(icon);
	const std::vector<std::uint8_t> background_dds = encode_dds(background);
	const std::vector<std::uint8_t> black_dds = encode_dds(solid(background.w, background.h, 0, 0, 0, 1));
	const std::vector<std::uint8_t> clear_dds = encode_dds(solid(background.w, background.h, 1, 1, 1, 0));
	std::vector<std::uint8_t> launcher, libc, launch_elf;
	if (icon_png.empty() || background_png.empty() || !read_whole(PS5_APP_ROOT "/tile-launcher.bin", launcher) ||
	    !read_whole(PS5_APP_ROOT "/sce_module/libc.prx", libc) || !read_whole(PS5_APP_ROOT "/rpcs3ps5-launch.elf", launch_elf)) {
		set_status("Could not make the tile for " + game.title + " (see ps5-title.log)");
		trace("tiles: icon %zu, background %zu, launcher %zu, libc %zu bytes", icon_png.size(), background_png.size(),
		      launcher.size(), libc.size());
		return;
	}

	set_status("Adding " + game.title + " to the PS5 home screen: installing the tile...");
	if (!helper.ready()) {
		set_status("The write helper stopped: " + game.title + " not added");
		return;
	}
	// param.json last: ShadowMountPlus installs the folder once it is complete
	const bool ok = helper.write_tile_file(id + "/eboot.bin", launcher) &&
	                helper.write_tile_file(id + "/sce_module/libc.prx", libc) &&
	                // the one-shot payload the tile sends to the ELF loader to start RPCS3 PS5 with the game
	                helper.write_tile_file(id + "/rpcs3ps5-launch.elf", launch_elf) &&
	                helper.write_tile_file(id + "/rpcs3-launch.txt", bytes(game.boot + "\n")) &&
	                helper.write_tile_file(id + "/sce_sys/icon0.png", icon_png) &&
	                helper.write_tile_file(id + "/sce_sys/pic0.png", background_png) &&
	                helper.write_tile_file(id + "/sce_sys/icon0.dds", icon_dds) &&
	                helper.write_tile_file(id + "/sce_sys/pic0.dds", background_dds) &&
	                helper.write_tile_file(id + "/sce_sys/pic1.dds", black_dds) &&
	                helper.write_tile_file(id + "/sce_sys/pic2.dds", clear_dds) &&
	                helper.write_tile_file(id + "/sce_sys/param.json", bytes(param_json(id, game.title)));
	if (!ok) {
		set_status("Writing the tile of " + game.title + " failed (see ps5-title.log)");
		return;
	}
	remember_tile(game, id, true);
	set_status(game.title + " is on the PS5 home screen (" + (cover.empty() ? "the game's own art" : "box art from GameTDB") +
	           "): close RPCS3 PS5 and it appears there");
}

bool remove_tile(Helper &helper, const Game &game)
{
	std::string id;
	bool existing = false;
	{
		std::lock_guard lock(g_list_mutex);
		id = assigned_tile_id(game, &existing);
	}
	if (!existing)
		return true;
	const std::int64_t r = helper.call("\x03rmtile/" + id, 0, nullptr, 0);
	trace("tiles: removing tile %s of %s: %lld", id.c_str(), game.title_id.c_str(), static_cast<long long>(r));
	if (r == 0)
		remember_tile(game, id, false);
	return r == 0;
}

// Path relative to the data folder ("games/WWE12"), or "" when outside it
std::string relative(const std::string &path)
{
	const std::string root = Library::root() + "/";
	return path.compare(0, root.size(), root) == 0 ? path.substr(root.size()) : std::string();
}

void delete_game(const Game &game, bool with_saves)
{
	set_status("Deleting " + game.title + "...");
	Helper helper;
	if (!helper.ready()) {
		set_status("The write helper did not start (is the ELF loader running?): " + game.title + " not deleted");
		return;
	}
	const std::string &serial = game.title_id;
	remove_tile(helper, game);

	std::vector<std::string> targets;
	const std::string boot = relative(game.boot);
	if (!boot.empty())
		targets.push_back(boot);
	const bool real_serial = serial.size() == 9 && serial != "????00000";
	if (real_serial) {
		targets.push_back("dev_hdd0/game/" + serial); // installed game data, updates, DLC
		targets.push_back("cache/" + serial);         // compiled PPU/SPU code and shaders
		targets.push_back("custom_configs/config_" + serial + ".yml");
		if (with_saves) {
			const std::string homes = Library::root() + "/dev_hdd0/home";
			if (DIR *users = ::opendir(homes.c_str())) {
				while (const dirent *user = ::readdir(users)) {
					if (user->d_name[0] == '.')
						continue;
					const std::string saves = homes + "/" + user->d_name + "/savedata";
					if (DIR *dir = ::opendir(saves.c_str())) {
						while (const dirent *save = ::readdir(dir))
							if (std::strncmp(save->d_name, serial.c_str(), 9) == 0)
								targets.push_back("dev_hdd0/home/" + std::string(user->d_name) + "/savedata/" + save->d_name);
						::closedir(dir);
					}
				}
				::closedir(users);
			}
		}
	}
	int failed = 0;
	for (const std::string &target : targets) {
		const std::int64_t r = helper.call("\x03rm/" + target, 0, nullptr, 0);
		trace("tiles: delete %s: %lld", target.c_str(), static_cast<long long>(r));
		if (r != 0)
			failed++;
	}
	g_rescan = true;
	set_status(failed ? game.title + " deleted, but " + std::to_string(failed) + " item(s) could not be (see ps5-title.log)"
	                  : game.title + (with_saves ? " and its saves deleted" : " deleted (its saves are kept)"));
}

void run_job(std::function<void()> job)
{
	if (g_busy.exchange(true)) {
		set_status("Busy: wait for the current job to finish");
		return;
	}
	std::thread([job = std::move(job)] {
		job();
		g_busy = false;
	}).detach();
}

} // namespace

bool tile_present(const Game &game)
{
	std::lock_guard lock(g_list_mutex);
	bool existing = false;
	assigned_tile_id(game, &existing);
	return existing;
}

bool tiles_busy()
{
	return g_busy;
}

void tile_add_async(const Game &game)
{
	Game copy = game;
	copy.icon = 0;
	run_job([copy] { add_tile(copy); });
}

void tile_remove_async(const Game &game)
{
	Game copy = game;
	copy.icon = 0;
	run_job([copy] {
		set_status("Removing " + copy.title + " from the PS5 home screen...");
		Helper helper;
		if (!helper.ready()) {
			set_status("The write helper did not start (is the ELF loader running?)");
			return;
		}
		set_status(remove_tile(helper, copy) ? copy.title + "'s tile files are removed: on the PS5 home screen, highlight its icon and press Options > Delete"
		                                     : "Removing the tile of " + copy.title + " failed (see ps5-title.log)");
	});
}

void game_delete_async(const Game &game, bool with_saves)
{
	Game copy = game;
	copy.icon = 0;
	run_job([copy, with_saves] { delete_game(copy, with_saves); });
}

bool take_rescan_wanted()
{
	return g_rescan.exchange(false);
}

std::string cover_path(const std::string &serial)
{
	return Library::root() + "/covers/" + serial + ".jpg";
}

namespace
{
std::string g_splash_boot;
}

void set_launch_splash(const std::string &boot)
{
	g_splash_boot = boot;
}

const std::string &launch_splash()
{
	return g_splash_boot;
}

} // namespace rpcs3ps5
