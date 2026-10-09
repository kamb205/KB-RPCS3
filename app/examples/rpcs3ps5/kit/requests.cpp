// RPCS3 PS5 - requests between the home screen and the program (requests.hpp).
// SPDX-License-Identifier: GPL-3.0-or-later

#include "requests.hpp"
#include "core.hpp"

#include <mutex>

namespace rpcs3ps5
{

namespace
{
std::mutex g_mutex;
Pending g_pending;
std::string g_status;
const char *g_core = "not linked yet (home screen only)";

std::string g_scope_id;    // "" = the global settings
std::string g_scope_title;
bool g_scope_custom = false;
CoreSettings g_values;
bool g_loaded = false;

// The last row (Vibration) is the title's own and shows only on the global settings
constexpr int kRows = 6;
constexpr const char *kLabels[kRows] = {"Resolution", "Frame limit", "Frame rate counter", "PPU decoder", "SPU decoder", "Vibration"};
constexpr int kScales[] = {100, 150, 200, 300};
constexpr const char *kScaleText[] = {"720p (100%)", "1080p (150%)", "1440p (200%)", "4K (300%)"};
constexpr const char *kLimitText[] = {"Auto", "Off", "30 FPS", "60 FPS"};
constexpr const char *kOnOff[] = {"Off", "On"};
constexpr const char *kPpuText[] = {"LLVM recompiler", "Interpreter (slow)"};
constexpr const char *kSpuText[] = {"LLVM recompiler", "ASMJIT recompiler", "Interpreter (slow)"};
constexpr const char *kVibrationText[] = {"Off", "Low", "Medium", "High"};

void load()
{
	core_settings_get(g_scope_id, g_values, &g_scope_custom);
	g_loaded = true;
}

int scale_index()
{
	for (int i = 3; i >= 0; --i)
		if (g_values.resolution_scale >= kScales[i])
			return i;
	return 0;
}
} // namespace

void request(Request what, int argument)
{
	std::lock_guard lock(g_mutex);
	g_pending = {what, argument};
}

Pending take_request()
{
	std::lock_guard lock(g_mutex);
	const Pending pending = g_pending;
	g_pending = {};
	return pending;
}

void set_status(const std::string &text)
{
	std::lock_guard lock(g_mutex);
	g_status = text;
}

std::string status_text()
{
	std::lock_guard lock(g_mutex);
	return g_status;
}

const char *core_status()
{
	return g_core;
}

void set_core_status(const char *text)
{
	g_core = text;
}

void settings_open(const std::string &title_id, const std::string &title)
{
	g_scope_id = title_id;
	g_scope_title = title;
	load();
}

bool settings_for_game()
{
	return !g_scope_id.empty();
}

const std::string &settings_game_id()
{
	return g_scope_id;
}

const std::string &settings_game_title()
{
	return g_scope_title;
}

bool settings_game_custom()
{
	return g_scope_custom;
}

int setting_count()
{
	return g_scope_id.empty() ? kRows : kRows - 1;
}

const char *setting_label(int row)
{
	return row >= 0 && row < kRows ? kLabels[row] : "";
}

const char *setting_text(int row)
{
	if (!g_loaded)
		load();
	switch (row) {
	case 0: return kScaleText[scale_index()];
	case 1: return kLimitText[g_values.frame_limit & 3];
	case 2: return kOnOff[g_values.perf_overlay ? 1 : 0];
	case 3: return kPpuText[g_values.ppu_decoder ? 1 : 0];
	case 4: return kSpuText[g_values.spu_decoder % 3];
	case 5: return kVibrationText[vibration_level() & 3];
	default: return "";
	}
}

void cycle_setting(int row)
{
	if (!g_loaded)
		load();
	switch (row) {
	case 0: g_values.resolution_scale = kScales[(scale_index() + 1) % 4]; break;
	case 1: g_values.frame_limit = (g_values.frame_limit + 1) % 4; break;
	case 2: g_values.perf_overlay = !g_values.perf_overlay; break;
	case 3: g_values.ppu_decoder = !g_values.ppu_decoder; break;
	case 4: g_values.spu_decoder = (g_values.spu_decoder + 1) % 3; break;
	case 5: set_vibration_level((vibration_level() + 1) % 4); return;
	default: return;
	}
	core_settings_set(g_scope_id, g_values);
	g_scope_custom = !g_scope_id.empty();
}

void reset_game_settings()
{
	if (g_scope_id.empty())
		return;
	core_settings_clear(g_scope_id);
	load();
}

} // namespace rpcs3ps5
