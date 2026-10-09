// RPCS3 PS5 - the emulator core as the title sees it (kit/core.cpp over ps5_frontend.h).
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace rpcs3ps5
{
// True when this build has the RPCS3 core linked in
bool core_linked();
// Once, at start: RPCS3's folders under /app0/rpcs3, its log, its callbacks
void core_init();
// Starts installing PS3UPDAT.PUP on a thread of its own; progress goes to set_status()
void core_install_firmware_async(const std::string &pup);
bool core_busy();
// Every .pkg in <root>/packages (PSN games, updates, DLC), on a thread of its own; progress to set_status()
void core_install_packages_async();
// Every .rap licence in <root>/exdata into the user's licence folder; how many were installed
int core_install_licenses();
// Runs a game until it ends (the home screen's Vulkan must be gone: RPCS3 takes the display)
void core_run(const std::string &path);

// One line to klog and /app0/rpcs3/ps5-title.log
void trace(const char *format, ...) __attribute__((format(printf, 1, 2)));

// The settings the home screen offers (see ps5_frontend.h): global for an empty title ID
struct CoreSettings
{
	int resolution_scale = 150; // percent
	int frame_limit = 0;        // 0 auto, 1 off, 2 = 30, 3 = 60
	int perf_overlay = 0;
	int ppu_decoder = 0; // 0 LLVM, 1 interpreter
	int spu_decoder = 0; // 0 LLVM, 1 ASMJIT, 2 interpreter
};
bool core_settings_get(const std::string &title_id, CoreSettings &out, bool *custom);
void core_settings_set(const std::string &title_id, const CoreSettings &in);
void core_settings_clear(const std::string &title_id);

// Pad rumble strength for every game: 0 off, 1 low, 2 medium (default), 3 high. Kept by the title in
// <root>/ps5-vibration.txt (RPCS3's config has no place for it)
int vibration_level();
void set_vibration_level(int level);

// One file out of a disc image, with RPCS3's own reader when the core is linked (it decrypts
// 3k3y and Redump+.dkey images); false when it cannot be read
bool core_iso_read(const std::string &iso, const std::string &inner, std::vector<std::uint8_t> &data);

// Every RPCS3 setting, as the PC version's settings dialog has them (rpcs3ps5_cfg_describe)
struct Setting
{
	std::string path;    // "Video|Vulkan|Asynchronous Texture Streaming"
	std::string section; // "Video"
	std::string label;   // "Vulkan > Asynchronous Texture Streaming"
	char kind = 's';     // b on/off, e choice, i whole number, f decimal, s text
	std::string value, def;
	std::vector<std::string> choices; // a choice's values, or {min, max}
	bool locked = false;              // fixed on the console
};
// Global for an empty title ID, else that game's (its own settings over the global ones)
std::vector<Setting> core_settings_all(const std::string &title_id);
bool core_setting_set(const std::string &title_id, const std::string &path, const std::string &value);
bool core_settings_apply_yaml(const std::string &title_id, const std::string &yaml);
bool core_settings_custom(const std::string &title_id);

// A boot the home screen asked for, for main.cpp to run once the screen has closed
void set_boot_path(const std::string &path);
std::string take_boot_path();
} // namespace rpcs3ps5
