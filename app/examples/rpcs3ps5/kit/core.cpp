// RPCS3 PS5 - the emulator core as the title sees it (core.hpp).
// SPDX-License-Identifier: GPL-3.0-or-later
//
// With RPCS3PS5_CORE (the build links RPCS3's archives) this drives rpcs3ps5_* in
// ps5_frontend.h; without it every call says the core is missing, so the home screen
// still builds and runs alone.

#include "core.hpp"
#include "library.hpp"
#include "requests.hpp"

#include "platform.h"

#include <sys/stat.h>
#include <dirent.h>

#include <algorithm>
#include <sstream>
#include <strings.h>
#include <vector>
#include <atomic>
#include <map>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <chrono>
#include <mutex>
#include <thread>

#ifdef RPCS3PS5_CORE
#include "ps5_frontend.h"

// util/vm_native_ps5.cpp: /app0/rpcs3/ps5-boot.log, usable before any constructor
extern "C" void rpcs3ps5_boot_log(const char *format, ...);
// ps5/src/import_check.c: every import the console left NULL, to the boot log
extern "C" void rpcs3ps5_import_check(void);

// The first constructor to run (priority 101, before RPCS3's globals reserve memory):
// if ps5-boot.log has this line, the console loaded the executable
__attribute__((constructor(101))) static void boot_marker()
{
	rpcs3ps5_boot_log("---- process start (RPCS3 PS5 title) ----");
	rpcs3ps5_import_check();
}
#endif

namespace rpcs3ps5
{

namespace
{
std::mutex g_boot_mutex;
std::string g_boot_path;
std::atomic<bool> g_busy{false};
} // namespace

void set_boot_path(const std::string &path)
{
	std::lock_guard lock(g_boot_mutex);
	g_boot_path = path;
}

std::string take_boot_path()
{
	std::lock_guard lock(g_boot_mutex);
	std::string path;
	path.swap(g_boot_path);
	return path;
}

bool core_busy()
{
	return g_busy;
}

// klog and /app0/rpcs3/ps5-title.log (read over FTP when the console has no klog capture),
// flushed at once so a crash keeps every line before it
void trace(const char *format, ...)
{
	static std::mutex mutex;
	static FILE *file = nullptr;
	char line[1024];
	va_list args;
	va_start(args, format);
	vsnprintf(line, sizeof(line), format, args);
	va_end(args);
	say("%s", line);
#ifdef RPCS3PS5_CORE
	rpcs3ps5_boot_log("%s", line);
#endif
	std::lock_guard lock(mutex);
	if (!file) {
		mkdir(Library::root().c_str(), 0777);
		file = std::fopen((Library::root() + "/ps5-title.log").c_str(), "a");
	}
	if (file) {
		std::fprintf(file, "[%.3f] %s\n", now_seconds(), line);
		std::fflush(file);
	}
}

namespace
{
std::atomic<int> g_vibration{-1};

std::string vibration_file()
{
	return Library::root() + "/ps5-vibration.txt";
}
} // namespace

int vibration_level()
{
	int level = g_vibration;
	if (level < 0) {
		level = 2;
		if (FILE *file = std::fopen(vibration_file().c_str(), "r")) {
			if (std::fscanf(file, "%d", &level) != 1 || level < 0 || level > 3)
				level = 2;
			std::fclose(file);
		}
		g_vibration = level;
	}
	return level;
}

void set_vibration_level(int level)
{
	g_vibration = level & 3;
	if (FILE *file = std::fopen(vibration_file().c_str(), "w")) {
		std::fprintf(file, "%d\n", level & 3);
		std::fclose(file);
	}
	trace("settings: vibration level %d", level & 3);
}

#ifdef RPCS3PS5_CORE

namespace
{
std::mutex g_pad_mutex;
rpcs3ps5_pad g_pad{};
double g_pad_time = 0.0;

// Remote input for a console nobody is sitting at: the Mac writes rpcs3/ps5-remote.txt over FTP
// (tools/ps5-input.sh), and each line holds a button or pushes a stick for a while, counted from when the
// line is read:
//   hold OPTIONS 0.5        a button down for 0.5 s   (pad names: CROSS CIRCLE SQUARE TRIANGLE L1 R1 L2 R2
//   stick 1 0 0 0 2.0       left stick right for 2 s   L3 R3 UP DOWN LEFT RIGHT OPTIONS SELECT TOUCH)
// The file is re-read whenever it changes; an expired line stops applying. A testing aid only.
uint32_t remote_button_bit(const std::string &name)
{
	static const std::pair<const char *, uint32_t> map[] = {
	    {"CROSS", RPCS3PS5_BTN_CROSS},     {"CIRCLE", RPCS3PS5_BTN_CIRCLE}, {"SQUARE", RPCS3PS5_BTN_SQUARE},
	    {"TRIANGLE", RPCS3PS5_BTN_TRIANGLE}, {"L1", RPCS3PS5_BTN_L1},       {"R1", RPCS3PS5_BTN_R1},
	    {"L2", RPCS3PS5_BTN_L2},           {"R2", RPCS3PS5_BTN_R2},         {"L3", RPCS3PS5_BTN_L3},
	    {"R3", RPCS3PS5_BTN_R3},           {"UP", RPCS3PS5_BTN_UP},         {"DOWN", RPCS3PS5_BTN_DOWN},
	    {"LEFT", RPCS3PS5_BTN_LEFT},       {"RIGHT", RPCS3PS5_BTN_RIGHT},   {"OPTIONS", RPCS3PS5_BTN_START},
	    {"START", RPCS3PS5_BTN_START},     {"SELECT", RPCS3PS5_BTN_SELECT}, {"TOUCH", RPCS3PS5_BTN_TOUCH},
	};
	for (const auto &entry : map)
		if (name == entry.first)
			return entry.second;
	return 0;
}

struct RemoteHold
{
	uint32_t buttons;
	float lx, ly, rx, ry;
	double until;
};
std::mutex g_remote_mutex;
std::vector<RemoteHold> g_remote;
std::string g_remote_key;

void apply_remote(double now, rpcs3ps5_pad &state)
{
	std::lock_guard lock(g_remote_mutex);
	const std::string path = Library::root() + "/ps5-remote.txt";
	struct stat st {};
	if (stat(path.c_str(), &st) != 0) {
		g_remote.clear();
		g_remote_key.clear();
		return;
	}
	char key[64];
	std::snprintf(key, sizeof(key), "%lld:%lld", (long long)st.st_mtime, (long long)st.st_size);
	if (g_remote_key != key) {
		g_remote_key = key;
		g_remote.clear();
		if (FILE *file = std::fopen(path.c_str(), "r")) {
			char line[256];
			while (std::fgets(line, sizeof(line), file)) {
				std::istringstream words(line);
				std::string command, name;
				if (!(words >> command))
					continue;
				double seconds = 0.5;
				if (command == "hold" && words >> name >> seconds) {
					if (const uint32_t bit = remote_button_bit(name))
						g_remote.push_back({bit, 0, 0, 0, 0, now + seconds});
				} else if (command == "stick") {
					RemoteHold hold{0, 0, 0, 0, 0, seconds};
					words >> hold.lx >> hold.ly >> hold.rx >> hold.ry >> hold.until;
					hold.until += now;
					g_remote.push_back(hold);
				}
			}
			std::fclose(file);
		}
		trace("remote: %zu command(s)", g_remote.size());
	}
	const auto axis = [](float v) { return (uint8_t)std::max(0.0f, std::min(255.0f, 128.0f + v * 127.0f)); };
	for (std::size_t i = 0; i < g_remote.size();) {
		RemoteHold &hold = g_remote[i];
		if (now >= hold.until) {
			g_remote.erase(g_remote.begin() + i);
			continue;
		}
		// No controller may be connected at all; RPCS3 ignores a disconnected pad
		state.connected = true;
		state.buttons |= hold.buttons;
		if (hold.lx != 0.0f || hold.ly != 0.0f) {
			state.lx = axis(hold.lx);
			state.ly = axis(-hold.ly);
		}
		if (hold.rx != 0.0f || hold.ry != 0.0f) {
			state.rx = axis(hold.rx);
			state.ry = axis(-hold.ry);
		}
		i++;
	}
}

// Both the run loop and RPCS3's pad thread ask; the pad is polled at most every 4 ms
void read_pad(rpcs3ps5_pad *out)
{
	std::lock_guard lock(g_pad_mutex);
	const double now = now_seconds();
	if (now - g_pad_time >= 0.004) {
		g_pad_time = now;
		struct pad pad{};
		pad_poll(&pad);
		const pad_reading *readings = nullptr;
		const int count = pad_readings(&readings);
		rpcs3ps5_pad state{};
		if (count > 0) {
			const pad_reading &r = readings[count - 1];
			state.connected = r.connected && !(r.buttons & PAD_INTERCEPTED);
			state.lx = r.left_x, state.ly = r.left_y, state.rx = r.right_x, state.ry = r.right_y;
			state.l2 = r.l2, state.r2 = r.r2;
			const uint32_t b = r.buttons;
			const struct { uint32_t from, to; } map[] = {
			    {PAD_CROSS, RPCS3PS5_BTN_CROSS}, {PAD_CIRCLE, RPCS3PS5_BTN_CIRCLE},
			    {PAD_SQUARE, RPCS3PS5_BTN_SQUARE}, {PAD_TRIANGLE, RPCS3PS5_BTN_TRIANGLE},
			    {PAD_L1, RPCS3PS5_BTN_L1}, {PAD_R1, RPCS3PS5_BTN_R1},
			    {PAD_L3, RPCS3PS5_BTN_L3}, {PAD_R3, RPCS3PS5_BTN_R3},
			    {PAD_UP, RPCS3PS5_BTN_UP}, {PAD_DOWN, RPCS3PS5_BTN_DOWN},
			    {PAD_LEFT, RPCS3PS5_BTN_LEFT}, {PAD_RIGHT, RPCS3PS5_BTN_RIGHT},
			    {PAD_OPTIONS, RPCS3PS5_BTN_START},
			    // The console keeps CREATE for itself: the touch pad click is SELECT
			    // (and, held 2 s, ends the game)
			    {PAD_TOUCH_PAD, RPCS3PS5_BTN_SELECT | RPCS3PS5_BTN_TOUCH},
			};
			for (const auto &m : map)
				if (b & m.from)
					state.buttons |= m.to;
			if (r.l2 > 40)
				state.buttons |= RPCS3PS5_BTN_L2;
			if (r.r2 > 40)
				state.buttons |= RPCS3PS5_BTN_R2;
		} else {
			// No new reading since the last poll: the pad is as it was
			state = g_pad;
		}
		apply_remote(now, state);
		g_pad = state;
	}
	*out = g_pad;
}

// The DualSense's motors are far stronger than the PS3 pad's: Settings → Vibration scales them
// (medium = a quarter of the motors' strength, the default)
void vibrate(uint8_t large, uint8_t small)
{
	constexpr float kStrength[] = {0.0f, 0.12f, 0.25f, 0.5f};
	const float strength = kStrength[vibration_level() & 3];
	pad_vibrate(large / 255.0f * strength, small / 255.0f * strength);
}

void log_line(const char *line)
{
	trace("%s", line);
}

void progress(const char *text)
{
	set_status(text);
}
} // namespace

bool core_linked()
{
	return true;
}

// The console starts the title with an owner-only umask, so RPCS3's files (config.yml, games.yml, logs)
// came out 0600; the title installer can't read those and dropped them on every install (2026-10-06).
// Files are made readable by everyone (still writable only by the title), the old ones included.
void open_up_file_modes(const std::string &root)
{
	umask(022);
	DIR *dir = opendir(root.c_str());
	if (!dir)
		return;
	int fixed = 0;
	while (const dirent *entry = readdir(dir)) {
		const std::string path = root + "/" + entry->d_name;
		struct stat st{};
		if (stat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode) && (st.st_mode & 0044) != 0044 &&
		    chmod(path.c_str(), (st.st_mode & 07777) | 0644) == 0)
			fixed++;
	}
	closedir(dir);
	if (fixed)
		trace("core_init: %d file(s) in %s made readable (umask was owner-only)", fixed, root.c_str());
}

void core_init()
{
	open_up_file_modes(Library::root());
	static const rpcs3ps5_host host{read_pad, log_line, progress, audio_start, audio_stop, vibrate};
	const std::string root = Library::root();
	trace("core_init: starting RPCS3 (data %s)", root.c_str());
	rpcs3ps5_init(root.substr(0, root.rfind('/')).c_str(), &host);
	trace("core_init: done, firmware '%s'", rpcs3ps5_firmware_version());
	// Folders RPCS3 makes from inside the title are not writable by the console's FTP server (another
	// process): "550 Permission denied" on the first game upload. Opened up, and the install drop
	// folders made, at every start.
	for (const char *folder : {"", "/games", "/packages", "/exdata", "/dev_hdd0", "/dev_hdd0/game"}) {
		const std::string path = root + folder;
		mkdir(path.c_str(), 0777);
		chmod(path.c_str(), 0777);
	}
	set_core_status("RPCS3 linked");
}

void core_install_firmware_async(const std::string &pup)
{
	if (g_busy.exchange(true))
		return;
	std::thread([pup] {
		trace("firmware: installing %s", pup.c_str());
		const int result = rpcs3ps5_install_firmware(pup.c_str());
		trace("firmware: result %d %s", result, rpcs3ps5_error());
		if (result != 0)
			set_status(std::string("Firmware install failed: ") + rpcs3ps5_error());
		else
			request(Request::rescan);
		g_busy = false;
	}).detach();
}

namespace
{
// The files in a folder whose names end in `ext` (any case), sorted
std::vector<std::string> files_with(const std::string &folder, const char *ext)
{
	std::vector<std::string> out;
	if (DIR *dir = opendir(folder.c_str())) {
		while (const dirent *entry = readdir(dir)) {
			const std::string name = entry->d_name;
			const std::size_t n = std::strlen(ext);
			if (name.size() > n && strcasecmp(name.c_str() + name.size() - n, ext) == 0)
				out.push_back(folder + "/" + name);
		}
		closedir(dir);
	}
	std::sort(out.begin(), out.end());
	return out;
}
} // namespace

void core_install_packages_async()
{
	const std::vector<std::string> packages = files_with(Library::root() + "/packages", ".pkg");
	if (packages.empty()) {
		set_status("No .pkg files in " + Library::root() + "/packages: copy them there over FTP, then choose this again");
		return;
	}
	if (g_busy.exchange(true)) {
		set_status("Wait for the install to finish");
		return;
	}
	std::thread([packages] {
		std::string list;
		for (const std::string &pkg : packages)
			list += pkg + "\n";
		trace("packages: installing %zu", packages.size());
		const int result = rpcs3ps5_install_package(list.c_str());
		trace("packages: result %d %s", result, rpcs3ps5_error());
		if (result != 0) {
			set_status(std::string("Package install failed: ") + rpcs3ps5_error());
		} else {
			set_status(std::to_string(packages.size()) + " package(s) installed  \xC2\xB7  the .pkg files can now be deleted from packages/");
			request(Request::rescan);
		}
		g_busy = false;
	}).detach();
}

int core_install_licenses()
{
	const std::string root = Library::root();
	const std::vector<std::string> raps = files_with(root + "/exdata", ".rap");
	const std::string target = root + "/dev_hdd0/home/00000001/exdata";
	for (const char *folder : {"/dev_hdd0/home", "/dev_hdd0/home/00000001", "/dev_hdd0/home/00000001/exdata"}) {
		mkdir((root + folder).c_str(), 0777);
		chmod((root + folder).c_str(), 0777);
	}
	int installed = 0;
	for (const std::string &rap : raps) {
		FILE *in = std::fopen(rap.c_str(), "rb");
		const std::string name = rap.substr(rap.find_last_of('/') + 1);
		FILE *out = in ? std::fopen((target + "/" + name).c_str(), "wb") : nullptr;
		bool ok = in && out;
		char buffer[4096];
		for (std::size_t n; ok && (n = std::fread(buffer, 1, sizeof(buffer), in)) > 0;)
			ok = std::fwrite(buffer, 1, n, out) == n;
		if (in)
			std::fclose(in);
		if (out)
			std::fclose(out);
		installed += ok;
		trace("licences: %s %s", name.c_str(), ok ? "installed" : "failed");
	}
	return installed;
}

void core_run(const std::string &path)
{
	trace("core_run: %s", path.c_str());
	if (rpcs3ps5_run(path.c_str()) != 0)
		set_status(std::string("Could not start the game: ") + rpcs3ps5_error());
	trace("core_run: back from %s (%s)", path.c_str(), rpcs3ps5_error());
}

bool core_settings_get(const std::string &title_id, CoreSettings &out, bool *custom)
{
	rpcs3ps5_settings settings{};
	if (rpcs3ps5_settings_get(title_id.c_str(), &settings, custom) != 0)
		return false;
	out = {settings.resolution_scale, settings.frame_limit, settings.perf_overlay, settings.ppu_decoder, settings.spu_decoder};
	return true;
}

void core_settings_set(const std::string &title_id, const CoreSettings &in)
{
	const rpcs3ps5_settings settings{in.resolution_scale, in.frame_limit, in.perf_overlay, in.ppu_decoder, in.spu_decoder};
	rpcs3ps5_settings_set(title_id.c_str(), &settings);
	trace("settings: saved for %s", title_id.empty() ? "all games" : title_id.c_str());
}

bool core_iso_read(const std::string &iso, const std::string &inner, std::vector<std::uint8_t> &data)
{
	data.resize(8u << 20);
	size_t size = 0;
	if (rpcs3ps5_iso_read(iso.c_str(), inner.c_str(), data.data(), data.size(), &size) != 0) {
		data.clear();
		return false;
	}
	data.resize(size);
	return true;
}

std::vector<Setting> core_settings_all(const std::string &title_id)
{
	std::vector<Setting> out;
	const char *text = rpcs3ps5_cfg_describe(title_id.c_str());
	for (const char *line = text; line && *line;) {
		const char *end = std::strchr(line, '\n');
		const std::string row(line, end ? end : line + std::strlen(line));
		line = end ? end + 1 : line + std::strlen(line);
		std::vector<std::string> fields;
		std::size_t from = 0;
		for (std::size_t tab; (tab = row.find('\t', from)) != std::string::npos; from = tab + 1)
			fields.push_back(row.substr(from, tab - from));
		fields.push_back(row.substr(from));
		if (fields.size() < 6 || fields[1].empty())
			continue;
		Setting setting;
		setting.path = fields[0];
		const std::size_t bar = setting.path.find('|');
		setting.section = setting.path.substr(0, bar);
		setting.label = bar == std::string::npos ? setting.path : setting.path.substr(bar + 1);
		for (std::size_t at; (at = setting.label.find('|')) != std::string::npos;)
			setting.label.replace(at, 1, " > ");
		setting.kind = fields[1][0];
		setting.value = fields[2];
		setting.def = fields[3];
		for (std::size_t start = 0; !fields[4].empty();) {
			const std::size_t sep = fields[4].find('\x1f', start);
			setting.choices.push_back(fields[4].substr(start, sep == std::string::npos ? std::string::npos : sep - start));
			if (sep == std::string::npos)
				break;
			start = sep + 1;
		}
		setting.locked = fields[5].find('L') != std::string::npos;
		out.push_back(std::move(setting));
	}
	return out;
}

bool core_setting_set(const std::string &title_id, const std::string &path, const std::string &value)
{
	const bool ok = rpcs3ps5_cfg_set(title_id.c_str(), path.c_str(), value.c_str()) == 0;
	trace("settings: %s = %s for %s: %s", path.c_str(), value.c_str(), title_id.empty() ? "all games" : title_id.c_str(), ok ? "saved" : "refused");
	return ok;
}

bool core_settings_apply_yaml(const std::string &title_id, const std::string &yaml)
{
	const int r = rpcs3ps5_cfg_apply_yaml(title_id.c_str(), yaml.c_str());
	trace("settings: recommended settings for %s: %d", title_id.c_str(), r);
	return r == 0;
}

bool core_settings_custom(const std::string &title_id)
{
	return rpcs3ps5_cfg_has_custom(title_id.c_str()) != 0;
}

void core_settings_clear(const std::string &title_id)
{
	rpcs3ps5_settings_clear(title_id.c_str());
	trace("settings: %s uses the global settings again", title_id.c_str());
}

#else

namespace
{
std::map<std::string, CoreSettings> g_local_settings; // the home screen alone keeps them in memory
}

bool core_settings_get(const std::string &title_id, CoreSettings &out, bool *custom)
{
	const auto found = g_local_settings.find(title_id);
	if (custom)
		*custom = !title_id.empty() && found != g_local_settings.end();
	out = found != g_local_settings.end() ? found->second : g_local_settings[""];
	return true;
}

void core_settings_set(const std::string &title_id, const CoreSettings &in)
{
	g_local_settings[title_id] = in;
}

void core_settings_clear(const std::string &title_id)
{
	g_local_settings.erase(title_id);
}

void core_install_packages_async()
{
	set_status("Package install needs the emulator core");
}

int core_install_licenses()
{
	return 0;
}

// Without the core there are no RPCS3 settings to list
std::vector<Setting> core_settings_all(const std::string &)
{
	return {};
}

bool core_setting_set(const std::string &, const std::string &, const std::string &)
{
	return false;
}

bool core_settings_apply_yaml(const std::string &, const std::string &)
{
	return false;
}

bool core_settings_custom(const std::string &)
{
	return false;
}

bool core_iso_read(const std::string &, const std::string &, std::vector<std::uint8_t> &)
{
	return false;
}

bool core_linked()
{
	return false;
}

void core_init()
{
}

void core_install_firmware_async(const std::string &)
{
	set_status("This build has no emulator core linked");
}

void core_run(const std::string &)
{
	set_status("This build has no emulator core linked");
}

#endif

} // namespace rpcs3ps5
