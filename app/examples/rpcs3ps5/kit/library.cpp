// RPCS3 PS5 - the PS3 game library (library.hpp).
// SPDX-License-Identifier: GPL-3.0-or-later

#include "library.hpp"
#include "core.hpp"

#include "gfx/renderer.hpp"

#include "stb_image.h" // implemented in base/VulkanglTFModel.cpp

#include <dirent.h>
#include <strings.h>
#include <sys/stat.h>

#include <algorithm>
#include <cstdio>
#include <cctype>
#include <cstring>
#include <sys/stat.h>

#ifndef PS5_APP_ROOT
#define PS5_APP_ROOT "/app0"
#endif

namespace rpcs3ps5
{

namespace
{

bool read_file(const std::string &path, std::vector<std::uint8_t> &data, std::size_t limit = 64u << 20)
{
	FILE *file = std::fopen(path.c_str(), "rb");
	if (!file)
		return false;
	std::fseek(file, 0, SEEK_END);
	const long size = std::ftell(file);
	std::fseek(file, 0, SEEK_SET);
	if (size <= 0 || static_cast<std::size_t>(size) > limit) {
		std::fclose(file);
		return false;
	}
	data.resize(static_cast<std::size_t>(size));
	const bool ok = std::fread(data.data(), 1, data.size(), file) == data.size();
	std::fclose(file);
	return ok;
}

std::uint32_t le32(const std::uint8_t *p);

// One file out of a plain ISO 9660 image (a decrypted PS3 disc dump): the primary volume
// descriptor's root directory, walked one path component at a time. Enough for PARAM.SFO and
// ICON0.PNG; encrypted (Redump/3k3y) images give nothing and the game is listed by its file name.
bool read_iso_file(const std::string &iso, const std::string &inner, std::vector<std::uint8_t> &data,
                   std::size_t limit = 8u << 20)
{
	FILE *file = std::fopen(iso.c_str(), "rb");
	if (!file)
		return false;
	auto read_at = [&](std::uint64_t offset, void *out, std::size_t bytes) {
		return std::fseek(file, static_cast<long>(offset), SEEK_SET) == 0 && std::fread(out, 1, bytes, file) == bytes;
	};
	std::uint8_t pvd[2048];
	bool ok = read_at(0x8000, pvd, sizeof(pvd)) && pvd[0] == 1 && std::memcmp(pvd + 1, "CD001", 5) == 0;
	std::uint32_t extent = ok ? (pvd[156 + 2] | (pvd[156 + 3] << 8) | (pvd[156 + 4] << 16) | (static_cast<std::uint32_t>(pvd[156 + 5]) << 24)) : 0;
	std::uint32_t size = ok ? (pvd[156 + 10] | (pvd[156 + 11] << 8) | (pvd[156 + 12] << 16) | (static_cast<std::uint32_t>(pvd[156 + 13]) << 24)) : 0;
	bool is_dir = true;
	std::size_t start = 0;
	while (ok && start <= inner.size()) {
		const std::size_t slash = inner.find('/', start);
		const std::string part = inner.substr(start, slash == std::string::npos ? std::string::npos : slash - start);
		start = slash == std::string::npos ? inner.size() + 1 : slash + 1;
		if (!is_dir || size > (16u << 20)) {
			ok = false;
			break;
		}
		std::vector<std::uint8_t> dir(size);
		ok = read_at(static_cast<std::uint64_t>(extent) * 2048, dir.data(), dir.size());
		bool found = false;
		for (std::size_t i = 0; ok && i < dir.size();) {
			const std::uint8_t length = dir[i];
			if (length == 0) {
				i = (i / 2048 + 1) * 2048; // records do not cross sectors
				continue;
			}
			if (i + length > dir.size() || length < 34)
				break;
			const std::uint8_t name_length = dir[i + 32];
			std::string name(reinterpret_cast<const char *>(&dir[i + 33]), name_length);
			if (const std::size_t semicolon = name.find(';'); semicolon != std::string::npos)
				name.resize(semicolon);
			bool same = name.size() == part.size();
			for (std::size_t c = 0; same && c < name.size(); ++c)
				same = std::toupper(static_cast<unsigned char>(name[c])) == std::toupper(static_cast<unsigned char>(part[c]));
			if (same) {
				extent = le32(&dir[i + 2]);
				size = le32(&dir[i + 10]);
				is_dir = (dir[i + 25] & 2) != 0;
				found = true;
				break;
			}
			i += length;
		}
		ok = ok && found;
	}
	if (ok && !is_dir && size > 0 && size <= limit) {
		data.resize(size);
		ok = read_at(static_cast<std::uint64_t>(extent) * 2048, data.data(), data.size());
	} else {
		ok = false;
	}
	std::fclose(file);
	return ok;
}

bool is_file(const std::string &path)
{
	struct stat info{};
	return stat(path.c_str(), &info) == 0 && S_ISREG(info.st_mode);
}

// Disc images RPCS3 boots directly (decrypted, 3k3y, or Redump with a .dkey beside it)
std::vector<std::string> iso_files(const std::string &path)
{
	std::vector<std::string> names;
	if (DIR *dir = opendir(path.c_str())) {
		while (dirent *entry = readdir(dir)) {
			const std::string name = entry->d_name;
			if (name.size() > 4 && name[0] != '.') {
				std::string extension = name.substr(name.size() - 4);
				for (char &c : extension)
					c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
				if (extension == ".iso")
					names.push_back(name);
			}
		}
		closedir(dir);
	}
	std::sort(names.begin(), names.end());
	return names;
}

std::vector<std::string> subfolders(const std::string &path)
{
	std::vector<std::string> names;
	if (DIR *dir = opendir(path.c_str())) {
		while (dirent *entry = readdir(dir)) {
			if (entry->d_name[0] == '.')
				continue;
			const std::string full = path + "/" + entry->d_name;
			struct stat info{};
			if (stat(full.c_str(), &info) == 0 && S_ISDIR(info.st_mode))
				names.push_back(entry->d_name);
		}
		closedir(dir);
	}
	std::sort(names.begin(), names.end());
	return names;
}

std::uint16_t le16(const std::uint8_t *p)
{
	return static_cast<std::uint16_t>(p[0] | (p[1] << 8));
}

std::uint32_t le32(const std::uint8_t *p)
{
	return p[0] | (p[1] << 8) | (p[2] << 16) | (static_cast<std::uint32_t>(p[3]) << 24);
}

std::uint32_t pack(float r, float g, float b)
{
	auto c = [](float v) { return static_cast<std::uint32_t>(std::clamp(v, 0.0f, 255.0f)); };
	return (c(r) << 16) | (c(g) << 8) | c(b);
}

// Three tones of an icon for the backdrop: its average, darkened; its average; its most
// saturated quarter.
void icon_palette(const std::uint8_t *rgba, int width, int height, Game &game)
{
	double sum[3] = {};
	double best[3] = {};
	double best_saturation = -1.0;
	long count = 0;
	for (int y = 0; y < height; y += 4) {
		for (int x = 0; x < width; x += 4) {
			const std::uint8_t *p = rgba + (static_cast<std::size_t>(y) * width + x) * 4;
			if (p[3] < 128)
				continue;
			sum[0] += p[0], sum[1] += p[1], sum[2] += p[2];
			++count;
			const int hi = std::max({p[0], p[1], p[2]}), lo = std::min({p[0], p[1], p[2]});
			const double saturation = hi ? (hi - lo) / static_cast<double>(hi) * hi : 0.0;
			if (saturation > best_saturation) {
				best_saturation = saturation;
				best[0] = p[0], best[1] = p[1], best[2] = p[2];
			}
		}
	}
	if (!count)
		return;
	const float r = static_cast<float>(sum[0] / count), g = static_cast<float>(sum[1] / count),
	            b = static_cast<float>(sum[2] / count);
	game.dark = pack(r * 0.3f, g * 0.3f, b * 0.3f);
	game.mid = pack(r * 0.7f, g * 0.7f, b * 0.7f);
	game.accent = pack(static_cast<float>(best[0]), static_cast<float>(best[1]), static_cast<float>(best[2]));
}

} // namespace

bool Sfo::load(const std::string &path)
{
	std::vector<std::uint8_t> data;
	return read_file(path, data, 1u << 20) && parse(data);
}

bool Sfo::parse(const std::vector<std::uint8_t> &data)
{
	if (data.size() < 20)
		return false;
	const std::uint8_t *p = data.data();
	if (std::memcmp(p, "\0PSF", 4) != 0)
		return false;
	const std::uint32_t keys = le32(p + 8), values = le32(p + 12), entries = le32(p + 16);
	if (20 + entries * 16ull > data.size())
		return false;
	for (std::uint32_t i = 0; i < entries; ++i) {
		const std::uint8_t *entry = p + 20 + i * 16;
		const std::uint32_t key_at = keys + le16(entry);
		const std::uint16_t format = le16(entry + 2);
		const std::uint32_t length = le32(entry + 4);
		const std::uint32_t value_at = values + le32(entry + 12);
		if (key_at >= data.size() || value_at + length > data.size())
			continue;
		const char *key = reinterpret_cast<const char *>(p + key_at);
		const std::string name(key, strnlen(key, data.size() - key_at));
		if (format == 0x0404 && length >= 4) {
			integers[name] = le32(p + value_at);
		} else {
			// 0x0204: UTF-8, NUL-terminated; 0x0004: raw
			const char *value = reinterpret_cast<const char *>(p + value_at);
			strings[name] = std::string(value, strnlen(value, length));
		}
	}
	return true;
}

std::string Sfo::get(const char *key, const char *fallback) const
{
	const auto found = strings.find(key);
	return found != strings.end() ? found->second : fallback;
}

std::string Library::root()
{
	return PS5_APP_ROOT "/rpcs3";
}

void Library::add(const std::string &folder, const std::string &boot, bool disc, hui::gfx::Renderer *renderer)
{
	Sfo sfo;
	if (!sfo.load(folder + "/PARAM.SFO"))
		return;
	std::vector<std::uint8_t> png;
	if (renderer)
		read_file(folder + "/ICON0.PNG", png);
	add_parsed(sfo, png, folder, boot, disc, renderer);
}

void Library::add_parsed(const Sfo &sfo, const std::vector<std::uint8_t> &png, const std::string &folder,
                         const std::string &boot, bool disc, hui::gfx::Renderer *renderer)
{
	Game game;
	game.category = sfo.get("CATEGORY");
	// Installed patches and game data (GD), save data (SD) and the like are not games
	if (!disc && game.category != "HG" && game.category != "DG" && game.category != "HM" && game.category != "CB")
		return;
	game.title = sfo.get("TITLE", "Untitled");
	game.title_id = sfo.get("TITLE_ID", "????00000");
	game.version = sfo.get("APP_VER", sfo.get("VERSION", "").c_str());
	game.path = folder;
	game.boot = boot;
	game.disc = disc;
	if (renderer) {
		if (!png.empty()) {
			int width = 0, height = 0, channels = 0;
			if (stbi_uc *rgba = stbi_load_from_memory(png.data(), static_cast<int>(png.size()), &width, &height,
			                                          &channels, 4)) {
				icon_palette(rgba, width, height, game);
				game.icon = renderer->create_texture(width, height, rgba);
				game.icon_width = width;
				game.icon_height = height;
				stbi_image_free(rgba);
			}
		}
	}
	games_.push_back(std::move(game));
}

void Library::scan(hui::gfx::Renderer *renderer)
{
	if (renderer)
		release(*renderer);
	games_.clear();
	const std::string base = root();

	const std::string discs = base + "/games";
	for (const std::string &name : subfolders(discs)) {
		const std::string folder = discs + "/" + name;
		if (is_file(folder + "/PS3_GAME/PARAM.SFO"))
			add(folder + "/PS3_GAME", folder, true, renderer);
		else if (is_file(folder + "/PARAM.SFO"))
			add(folder, folder, true, renderer);
	}
	// ISO files: PARAM.SFO and ICON0.PNG read out of the image; listed by file name when they cannot be
	for (const std::string &name : iso_files(discs)) {
		const std::string path = discs + "/" + name;
		std::vector<std::uint8_t> sfo_bytes, png;
		Sfo sfo;
		// RPCS3's reader first (it decrypts), then the plain ISO 9660 one
		const bool through_core = rpcs3ps5::core_iso_read(path, "PS3_GAME/PARAM.SFO", sfo_bytes);
		if ((through_core || read_iso_file(path, "PS3_GAME/PARAM.SFO", sfo_bytes)) && sfo.parse(sfo_bytes)) {
			if (renderer && !(through_core && rpcs3ps5::core_iso_read(path, "PS3_GAME/ICON0.PNG", png)))
				read_iso_file(path, "PS3_GAME/ICON0.PNG", png);
			add_parsed(sfo, png, path, path, true, renderer);
			continue;
		}
		Game game;
		game.title = name.substr(0, name.size() - 4);
		game.title_id = "ISO";
		game.category = "DG";
		game.path = discs + "/" + name;
		game.boot = game.path;
		game.disc = true;
		games_.push_back(std::move(game));
	}
	const std::string installed = base + "/dev_hdd0/game";
	for (const std::string &name : subfolders(installed))
		add(installed + "/" + name, installed + "/" + name, false, renderer);

	std::sort(games_.begin(), games_.end(), [](const Game &a, const Game &b) { return a.title < b.title; });

	pup_present_ = is_file(base + "/PS3UPDAT.PUP");
	firmware_version_.clear();
	std::vector<std::uint8_t> version;
	if (read_file(base + "/dev_flash/vsh/etc/version.txt", version, 4096)) {
		// "release:04.9200:..." -> "4.92"
		const std::string text(version.begin(), version.end());
		const std::size_t at = text.find("release:");
		if (at != std::string::npos && text.size() >= at + 15) {
			const std::string number = text.substr(at + 8, 7); // 04.9200
			firmware_version_ = std::to_string(std::atoi(number.c_str())) + "." + number.substr(3, 2);
		}
	}
	firmware_installed_ = !firmware_version_.empty();
}

void Library::release(hui::gfx::Renderer &renderer)
{
	for (Game &game : games_) {
		if (game.icon) {
			renderer.destroy_texture(game.icon);
			game.icon = 0;
		}
	}
}

bool game_file(const Game &game, const std::string &name, std::vector<std::uint8_t> &data)
{
	const std::string &boot = game.boot;
	const bool iso = boot.size() > 4 && strcasecmp(boot.c_str() + boot.size() - 4, ".iso") == 0;
	if (!iso)
		return read_file(game.path + "/" + name, data);
	return rpcs3ps5::core_iso_read(boot, "PS3_GAME/" + name, data) || read_iso_file(boot, "PS3_GAME/" + name, data, 32u << 20);
}

Library &library()
{
	static Library instance;
	return instance;
}

} // namespace rpcs3ps5
