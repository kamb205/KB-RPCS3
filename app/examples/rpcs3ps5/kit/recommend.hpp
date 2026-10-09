// RPCS3 PS5 - recommended settings for each game: RPCS3's own per-game settings database (the one the PC
// version downloads from api.rpcs3.net/config; a copy ships with the title) plus what the console needs.
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "library.hpp"

#include <string>
#include <vector>

namespace rpcs3ps5
{

struct SettingChange
{
	std::string path;   // "Video|MSAA"
	std::string label;  // "GPU > MSAA"
	std::string from;   // the game's value now
	std::string to;     // the recommended one
	std::string why;    // one line in plain words
	bool fixed = false; // the console keeps its own value (the change is not applied)
};

struct Recommendation
{
	bool in_database = false;           // RPCS3's database has settings for this game
	std::string yaml;                   // what is applied (database entry + console additions)
	std::vector<SettingChange> changes; // against the game's settings now (only what differs)
	bool own_settings = false;          // the game already has settings of its own
};

Recommendation recommendation_for(const Game &game);
// Applies them as the game's own settings; false when RPCS3 refused some
bool apply_recommendation(const Game &game);
// Back to the global settings (the game's own are removed)
void revert_to_global(const Game &game);

// Games the library lists that were never offered their recommended settings (each is offered once)
std::vector<std::size_t> unoffered_games(const std::vector<Game> &games);
void mark_offered(const Game &game);

// The settings page names the PC version gives RPCS3's config sections
std::string section_title(const std::string &section);

} // namespace rpcs3ps5
