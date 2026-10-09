// RPCS3 PS5 - PS3 games on the PS5's own home screen, and deleting games.
// SPDX-License-Identifier: GPL-3.0-or-later
//
// A game's tile is a small title of its own (/data/homebrew/PPSA8nnnn, installed by ShadowMountPlus) whose
// eboot (tools/launcher) starts RPCS3 PS5 with "--boot <the game>". Its icon (512x512) and background
// (3840x2160) are made here from the game's box art (GameTDB) or, without one, the game's own ICON0/PIC1.
// The title's sandbox can't write /data/homebrew: the files go through the write helper payload
// (tools/iohelper, version 2), which also deletes and uninstalls.
#pragma once

#include "library.hpp"

#include <string>

namespace rpcs3ps5
{

// Whether this game has a tile (kept in <root>/ps5-tiles.txt with its tile ID, PPSA8nnnn)
bool tile_present(const Game &game);

// Work on a thread of its own; progress and the outcome go to set_status()
bool tiles_busy();
void tile_add_async(const Game &game);
void tile_remove_async(const Game &game);
// The game's files, installed data, caches, own settings and tile; its saves too when asked
void game_delete_async(const Game &game, bool with_saves);
// True once after a delete finished (the library has to be scanned again)
bool take_rescan_wanted();
// The box art saved when the tile was made (<root>/covers/<serial>.jpg; may not exist)
std::string cover_path(const std::string &serial);
// Started from a tile: the program shows that game's launch screen instead of the home screen
void set_launch_splash(const std::string &boot);
const std::string &launch_splash();

} // namespace rpcs3ps5
