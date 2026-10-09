// RPCS3 PS5 - what the home screen asks of the program, and what it shows back.
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The screen only draws and queues requests; the program (rpcs3ps5.cpp) takes them
// once a frame and does the work: rescans, installs, boots the emulator.

#pragma once

#include <string>

namespace rpcs3ps5
{

enum class Request
{
	none,
	rescan,
	boot,            // argument: the game's index in library().games()
	game_settings,   // argument: the game's index
	install_firmware,
	install_packages,
	install_licenses,
	toggle_setting,  // argument: the settings row
	tile_add,        // argument: the game's index (its tile on the PS5 home screen)
	tile_remove,     // argument: the game's index
	delete_game,     // argument: the game's index (its saves are kept)
	delete_game_and_saves, // argument: the game's index
};

struct Pending
{
	Request what = Request::none;
	int argument = 0;
};

// Queued by the screen (the last request of a frame wins)
void request(Request what, int argument = 0);
// Taken by the program
Pending take_request();

// One line at the bottom of the screen ("Installing firmware: 42%"); empty hides it
void set_status(const std::string &text);
std::string status_text();

// The emulator core's state, for the System tab
const char *core_status();
void set_core_status(const char *text);

// The settings the Settings tab shows: global, or one game's after settings_open(its title ID).
// cycle_setting changes and saves at once; a game's first change gives it settings of its own.
void settings_open(const std::string &title_id, const std::string &title);
bool settings_for_game();
const std::string &settings_game_id();
const std::string &settings_game_title();
bool settings_game_custom();
int setting_count();
const char *setting_label(int row);
const char *setting_text(int row);
void cycle_setting(int row);
void reset_game_settings();

} // namespace rpcs3ps5
