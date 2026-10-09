// RPCS3 PS5 - the PS3 game library: what the home screen lists.
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Games are found where RPCS3 itself keeps them, under the title's own folder
// (/app0 is /data/homebrew/PPSA99303 on the console, writable, and copied to over FTP):
//
//   /app0/rpcs3/games/<any folder>/PS3_GAME/PARAM.SFO   a disc game, dumped as a folder
//   /app0/rpcs3/games/<any folder>/PARAM.SFO            the same without PS3_GAME
//   /app0/rpcs3/dev_hdd0/game/<TITLEID>/PARAM.SFO       an installed (PSN/HDD) game
//
// Each game's PARAM.SFO gives its name, title ID, version and category; its
// ICON0.PNG (320x176) becomes the cover.

#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace hui::gfx
{
class Renderer;
}

namespace rpcs3ps5
{

// PARAM.SFO: a PS3 game's metadata, as key -> string or integer
struct Sfo
{
	std::map<std::string, std::string> strings;
	std::map<std::string, std::uint32_t> integers;

	bool load(const std::string &path);
	bool parse(const std::vector<std::uint8_t> &data);
	std::string get(const char *key, const char *fallback = "") const;
};

struct Game
{
	std::string title;    // TITLE
	std::string title_id; // TITLE_ID, e.g. BLUS30443
	std::string version;  // APP_VER (or VERSION)
	std::string category; // DG disc game, HG HDD game, ...
	std::string path;     // the folder that holds PARAM.SFO
	std::string boot;     // what RPCS3 boots: the folder of a disc game, the game's folder
	bool disc = false;
	std::uint32_t icon = 0; // texture (0: none)
	int icon_width = 0, icon_height = 0;
	// Colours taken from the icon, for the backdrop
	std::uint32_t dark = 0x101420, mid = 0x2a3550, accent = 0x7ff0d8;
};

class Library
{
public:
	// The RPCS3 data folder: /app0/rpcs3 on the console
	static std::string root();

	// Scans the game folders. Icons are uploaded with `renderer` when one is given.
	void scan(hui::gfx::Renderer *renderer);
	void release(hui::gfx::Renderer &renderer);

	const std::vector<Game> &games() const
	{
		return games_;
	}

	// Firmware: PS3UPDAT.PUP waiting in the data folder, and an installed one
	bool firmware_pup_present() const
	{
		return pup_present_;
	}
	bool firmware_installed() const
	{
		return firmware_installed_;
	}
	const std::string &firmware_version() const
	{
		return firmware_version_;
	}

private:
	void add(const std::string &folder, const std::string &boot, bool disc, hui::gfx::Renderer *renderer);
	void add_parsed(const Sfo &sfo, const std::vector<std::uint8_t> &png, const std::string &folder,
	                const std::string &boot, bool disc, hui::gfx::Renderer *renderer);

	std::vector<Game> games_;
	bool pup_present_ = false;
	bool firmware_installed_ = false;
	std::string firmware_version_;
};

// The one library the program and its screen share
Library &library();
// One of a game's files beside its PARAM.SFO (ICON0.PNG, PIC1.PNG, ...), out of its folder or disc image
bool game_file(const Game &game, const std::string &name, std::vector<std::uint8_t> &data);

} // namespace rpcs3ps5
