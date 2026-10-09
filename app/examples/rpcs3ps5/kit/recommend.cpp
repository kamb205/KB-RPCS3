// RPCS3 PS5 - recommended settings for each game (recommend.hpp).
// SPDX-License-Identifier: GPL-3.0-or-later

#include "recommend.hpp"
#include "core.hpp"

#include "json.hpp" // nlohmann::json, external/tinygltf

#include <algorithm>
#include <fstream>
#include <map>
#include <mutex>
#include <set>
#include <sstream>

#ifndef PS5_APP_ROOT
#define PS5_APP_ROOT "/app0"
#endif

namespace rpcs3ps5
{

namespace
{

std::mutex g_mutex;
bool g_loaded = false;
std::map<std::string, std::string> g_database; // serial -> config YAML

// RPCS3's per-game settings (api.rpcs3.net/config/?api=v1, as the PC version downloads it), shipped with the
// title: the title's sandbox has no network and that address is HTTPS only
void load_database()
{
	if (g_loaded)
		return;
	g_loaded = true;
	std::ifstream in(PS5_APP_ROOT "/rpcs3-config-db.json");
	std::stringstream text;
	text << in.rdbuf();
	const nlohmann::json db = nlohmann::json::parse(text.str(), nullptr, false);
	if (db.is_discarded() || !db.is_object() || db.find("games") == db.end() || !db["games"].is_object()) {
		trace("recommend: no settings database (%s)", PS5_APP_ROOT "/rpcs3-config-db.json");
		return;
	}
	for (const auto &[serial, entry] : db["games"].items())
		if (entry.is_object() && entry.find("config") != entry.end() && entry["config"].is_string())
			g_database[serial] = entry["config"].get<std::string>();
	trace("recommend: settings database with %zu games", g_database.size());
}

// What the console adds for every game: the emulator falls behind real time now and then on the PS5, and
// without time stretching the sound breaks up each time (GTA IV, 2026-10-08)
const char *kConsoleAdditions = "Audio:\n  Enable Time Stretching: true\n  Desired Audio Buffer Duration: 100\n";

// "Video:\n  Vulkan:\n    X: y\n  Libraries Control:\n    - a\n" -> {"Video|Vulkan|X", "y"}, {"...|Libraries Control", "a"}
std::vector<std::pair<std::string, std::string>> flatten(const std::string &yaml)
{
	std::vector<std::pair<std::string, std::string>> out;
	std::vector<std::pair<int, std::string>> stack; // indent, name
	std::istringstream lines(yaml);
	for (std::string line; std::getline(lines, line);) {
		const std::size_t first = line.find_first_not_of(' ');
		if (first == std::string::npos)
			continue;
		const int indent = static_cast<int>(first);
		std::string body = line.substr(first);
		if (body.rfind("- ", 0) == 0) {
			// a list entry of the last key
			if (!out.empty() && !stack.empty())
				out.back().second += (out.back().second.empty() ? "" : ", ") + body.substr(2);
			continue;
		}
		while (!stack.empty() && stack.back().first >= indent)
			stack.pop_back();
		const std::size_t colon = body.find(':');
		if (colon == std::string::npos)
			continue;
		const std::string key = body.substr(0, colon);
		std::string value = colon + 1 < body.size() ? body.substr(colon + 1) : "";
		value.erase(0, value.find_first_not_of(' '));
		std::string path;
		for (const auto &[i, name] : stack)
			path += name + "|";
		path += key;
		if (value.empty()) {
			stack.push_back({indent, key});
			// a key whose list entries follow ("Libraries Control:")
			out.push_back({path, ""});
			continue;
		}
		out.push_back({path, value});
	}
	// keys that only opened a level have no value of their own
	out.erase(std::remove_if(out.begin(), out.end(), [&](const auto &e) {
		if (!e.second.empty())
			return false;
		return std::any_of(out.begin(), out.end(), [&](const auto &o) { return o.first.rfind(e.first + "|", 0) == 0; });
	}), out.end());
	return out;
}

std::string pretty_path(const std::string &path)
{
	const std::size_t bar = path.find('|');
	std::string label = section_title(path.substr(0, bar)) + (bar == std::string::npos ? "" : " > " + path.substr(bar + 1));
	for (std::size_t at; (at = label.find('|')) != std::string::npos;)
		label.replace(at, 1, " > ");
	return label;
}

// Why RPCS3 changes a setting for a game, in plain words (the settings its database uses)
std::string explain(const std::string &path)
{
	static const std::map<std::string, std::string> kWhy = {
	    {"Write Color Buffers", "Copies what the game draws back to its memory: needed for effects the game reads back (fixes missing or black graphics)."},
	    {"Read Color Buffers", "Lets the game's own drawings be read back by the GPU: fixes missing images and effects."},
	    {"Write Depth Buffer", "Copies depth information back to the game's memory: fixes shadows and effects that read it."},
	    {"Read Depth Buffer", "Lets the game read depth information it wrote: fixes shadows and special effects."},
	    {"Accurate ZCULL stats", "Exact visibility counting: fixes flickering objects; off is faster when the game does not need it."},
	    {"Relaxed ZCULL Sync", "Answers visibility queries without waiting: faster, safe for this game."},
	    {"Frame limit", "The frame rate the game is held to: stops it running too fast or fixes its timing."},
	    {"Sleep Timers Accuracy", "How exactly the game's sleeps are timed: fixes stutter and timing problems."},
	    {"Asynchronous Texture Streaming", "Uploads textures in the background: less stutter."},
	    {"Multithreaded RSX", "Spreads the graphics work over more CPU threads: faster for games heavy on graphics."},
	    {"SPU Block Size", "How much SPU code is compiled at once: the right size is faster or avoids crashes."},
	    {"Strict Rendering Mode", "Draws exactly as the PS3 does, slower: fixes graphics glitches."},
	    {"Accurate RSX reservation access", "Exact memory syncing between CPU and GPU: fixes freezes and glitches."},
	    {"SPU XFloat Accuracy", "How exactly SPU maths matches the PS3: fixes physics problems such as falling through the world."},
	    {"MSAA", "Anti-aliasing: off is faster and avoids graphics problems in this game."},
	    {"Handle RSX Memory Tiling", "Emulates the PS3's tiled graphics memory: fixes garbled graphics."},
	    {"PPU Decoder", "How the PS3's main CPU code is run: the safe choice for this game."},
	    {"Disable Vertex Cache", "Reloads geometry every frame: fixes stretched or missing models."},
	    {"Anisotropic Filter Override", "Texture sharpness at angles: the value that works for this game."},
	    {"Enable Buffering", "Buffers graphics commands: the setting that works for this game."},
	    {"Allow Host GPU Labels", "Lets the GPU signal the game directly: faster, safe for this game."},
	    {"SPU Decoder", "How SPU code is run: the safe choice for this game."},
	    {"Shader Precision", "Precision of graphics shaders: lower is faster, fine for this game."},
	    {"Emulate Special Depth Comparison", "Copies a PS3 depth trick: fixes shadows."},
	    {"Shader Mode", "How graphics shaders are built (the console keeps its own choice)."},
	    {"Resolution", "The PS3 output mode the game expects."},
	    {"Disable SPU GETLLAR Spin Optimization", "Turns off an SPU shortcut that breaks this game."},
	    {"Max SPURS Threads", "How many SPU worker threads the game may use: fixes stutter or hangs."},
	    {"Accurate SPU DMA", "Exact SPU memory transfers: fixes crashes and glitches."},
	    {"Emulate BD-ROM Read Speed", "Reads the disc at PS3 speed: some games break when loading is too fast."},
	    {"Emulate HDD Read Speed", "Reads the hard drive at PS3 speed: some games break when loading is too fast."},
	    {"RSX FIFO Fetch Accuracy", "How exactly graphics commands are read: fixes freezes."},
	    {"Disable Blit Engine Upscaling", "Keeps some copies at native size: fixes blurry or broken images when upscaling."},
	    {"SPU loop detection", "Skips idle SPU loops: the right choice for this game."},
	    {"Debug Console Mode", "Gives the game the extra memory of a PS3 development kit."},
	    {"Enable Time Stretching", "Stretches the sound instead of breaking up when the game slows down (PS5)."},
	    {"Desired Audio Buffer Duration", "A larger sound buffer: no crackling when the game slows down briefly (PS5)."},
	    {"MFC Commands Shuffling Limit", "Reorders SPU memory commands as the PS3 does: fixes timing bugs."},
	    {"Vblank NTSC Fixup", "Exact 59.94 Hz timing: fixes speed problems."},
	    {"Libraries Control", "Uses the PS3's own system libraries for parts the emulator does not cover well."},
	};
	const std::string name = path.substr(path.rfind('|') == std::string::npos ? 0 : path.rfind('|') + 1);
	const auto it = kWhy.find(name);
	return it != kWhy.end() ? it->second : "RPCS3 recommends this value for this game.";
}

std::string known_file()
{
	return Library::root() + "/ps5-offered-games.txt";
}

std::set<std::string> read_offered()
{
	std::set<std::string> serials;
	std::ifstream in(known_file());
	for (std::string line; std::getline(in, line);)
		if (!line.empty())
			serials.insert(line);
	return serials;
}

std::string game_key(const Game &game)
{
	return game.title_id.size() == 9 && game.title_id != "????00000" ? game.title_id : game.boot;
}

} // namespace

std::string section_title(const std::string &section)
{
	if (section == "Core")
		return "CPU";
	if (section == "Video")
		return "GPU";
	if (section == "Input/Output")
		return "I/O";
	if (section == "Net")
		return "Network";
	if (section == "Miscellaneous" || section == "Savestate")
		return "Emulator";
	return section;
}

Recommendation recommendation_for(const Game &game)
{
	Recommendation out;
	std::string entry;
	{
		std::lock_guard lock(g_mutex);
		load_database();
		const auto it = g_database.find(game.title_id);
		if (it != g_database.end()) {
			out.in_database = true;
			entry = it->second;
		}
	}
	out.yaml = entry + (entry.empty() || entry.back() == '\n' ? "" : "\n") + kConsoleAdditions;
	out.own_settings = core_settings_custom(game.title_id);

	const std::vector<Setting> now = core_settings_all(game.title_id);
	for (const auto &[path, value] : flatten(out.yaml)) {
		const auto it = std::find_if(now.begin(), now.end(), [&](const Setting &s) { return s.path == path; });
		SettingChange change;
		change.path = path;
		change.label = pretty_path(path);
		change.to = value;
		change.from = it != now.end() ? it->value : "";
		change.fixed = it != now.end() && it->locked;
		change.why = explain(path);
		if (it != now.end() && it->value == value)
			continue; // already so
		out.changes.push_back(std::move(change));
	}
	return out;
}

bool apply_recommendation(const Game &game)
{
	const Recommendation rec = recommendation_for(game);
	mark_offered(game);
	return core_settings_apply_yaml(game.title_id, rec.yaml);
}

void revert_to_global(const Game &game)
{
	core_settings_clear(game.title_id);
	mark_offered(game);
}

std::vector<std::size_t> unoffered_games(const std::vector<Game> &games)
{
	std::lock_guard lock(g_mutex);
	const std::set<std::string> offered = read_offered();
	std::vector<std::size_t> out;
	for (std::size_t i = 0; i < games.size(); ++i)
		if (games[i].title_id != "ISO" && !offered.count(game_key(games[i])))
			out.push_back(i);
	return out;
}

void mark_offered(const Game &game)
{
	std::lock_guard lock(g_mutex);
	std::set<std::string> offered = read_offered();
	if (!offered.insert(game_key(game)).second)
		return;
	std::ofstream out(known_file(), std::ios::trunc);
	for (const std::string &serial : offered)
		out << serial << '\n';
}

} // namespace rpcs3ps5
