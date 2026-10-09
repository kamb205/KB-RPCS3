// RPCS3 PS5 - the emulator's interface to the PS5 title (C, so the title needs none of
// RPCS3's headers). The title owns the console: the pad, the display between games,
// the home screen. RPCS3 owns the display while a game runs.
// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// The pad as the title reads it, once a frame
struct rpcs3ps5_pad
{
	bool connected;
	uint32_t buttons; // RPCS3PS5_BTN_*
	uint8_t lx, ly, rx, ry; // 0..255, 128 = centre
	uint8_t l2, r2;         // 0..255
};

enum
{
	RPCS3PS5_BTN_CROSS = 1u << 0,
	RPCS3PS5_BTN_CIRCLE = 1u << 1,
	RPCS3PS5_BTN_SQUARE = 1u << 2,
	RPCS3PS5_BTN_TRIANGLE = 1u << 3,
	RPCS3PS5_BTN_L1 = 1u << 4,
	RPCS3PS5_BTN_R1 = 1u << 5,
	RPCS3PS5_BTN_L2 = 1u << 6,
	RPCS3PS5_BTN_R2 = 1u << 7,
	RPCS3PS5_BTN_L3 = 1u << 8,
	RPCS3PS5_BTN_R3 = 1u << 9,
	RPCS3PS5_BTN_UP = 1u << 10,
	RPCS3PS5_BTN_DOWN = 1u << 11,
	RPCS3PS5_BTN_LEFT = 1u << 12,
	RPCS3PS5_BTN_RIGHT = 1u << 13,
	RPCS3PS5_BTN_START = 1u << 14,  // OPTIONS
	RPCS3PS5_BTN_SELECT = 1u << 15, // CREATE
	RPCS3PS5_BTN_TOUCH = 1u << 16,  // touch pad click: held 2 s ends the game
};

// What the title lends the emulator
struct rpcs3ps5_host
{
	void (*read_pad)(struct rpcs3ps5_pad *pad); // the latest pad state
	void (*log)(const char *line);              // one line to klog / the title's log
	void (*progress)(const char *text);         // shown while installing or booting
	// The console's sound output: 48 kHz interleaved stereo 16-bit, `fill` called from a
	// thread of its own for each block. false when there is none.
	bool (*audio_start)(void (*fill)(int16_t *frames, int count, void *user), void *user);
	void (*audio_stop)(void);
	// The pad's rumble: large and small motor, 0..255 each (0, 0 stops it)
	void (*vibrate)(uint8_t large, uint8_t small);
};

// Once, before anything else: RPCS3's data under `root` (e.g. "/app0", giving /app0/rpcs3/)
int rpcs3ps5_init(const char *root, const struct rpcs3ps5_host *host);

// PS3UPDAT.PUP -> dev_flash. 0 on success; rpcs3ps5_error() says why not.
int rpcs3ps5_install_firmware(const char *pup_path);
// The installed firmware's version ("4.92"), or "" when none is
const char *rpcs3ps5_firmware_version(void);

// Installs PKGs (PSN games, updates, DLC; one path per line, RPCS3 orders them) into dev_hdd0, reporting
// progress through host.progress. 0 on success; rpcs3ps5_error() says why not.
int rpcs3ps5_install_package(const char *pkg_paths);

// Boots a game (a disc folder, a dev_hdd0/game folder, or an EBOOT.BIN) and runs it until
// it ends or the player holds the touch pad for 2 s. 0 when it ran; rpcs3ps5_error() otherwise.
int rpcs3ps5_run(const char *path);

const char *rpcs3ps5_error(void);

// The settings the home screen offers, global (title_id NULL or "") or for one game (its title ID).
// A game's settings are a full config of its own (RPCS3's custom config), made from the global one
// the first time it is changed; clearing it makes the game use the global settings again.
struct rpcs3ps5_settings
{
	int resolution_scale; // percent: 100 (720p), 150, 200, 300 (4K)
	int frame_limit;      // 0 auto, 1 off, 2 = 30, 3 = 60
	int perf_overlay;     // 0 off, 1 on (frame rate and frame time on screen)
	int ppu_decoder;      // 0 LLVM, 1 interpreter
	int spu_decoder;      // 0 LLVM, 1 ASMJIT, 2 interpreter
};
// Reads; *custom (may be NULL) says whether the game has settings of its own
int rpcs3ps5_settings_get(const char *title_id, struct rpcs3ps5_settings *out, bool *custom);
// Writes and saves
int rpcs3ps5_settings_set(const char *title_id, const struct rpcs3ps5_settings *in);
// Removes a game's own settings
int rpcs3ps5_settings_clear(const char *title_id);

// Reads one file out of a PS3 disc image with RPCS3's own reader (decrypted, 3k3y, or Redump with a
// .dkey beside it), e.g. inner = "PS3_GAME/PARAM.SFO". 0 when the whole file fit in buffer.
int rpcs3ps5_iso_read(const char *iso, const char *inner, void *buffer, size_t capacity, size_t *size);

// Every setting of RPCS3's config (as the PC version's settings dialog has them), global for an empty or
// null title ID, else the game's own (its custom config over the global one). One line per setting:
//   section|...|name \t kind \t value \t default \t choices \t flags
// kind: b (on/off), e (choice), i (whole number), f (decimal), s (text); choices: \x1f-separated (a choice),
// or "min\x1fmax" (a number); flags: L = fixed on the console (shown, not changeable). Valid until the next call.
const char *rpcs3ps5_cfg_describe(const char *title_id);
// Sets one setting (path as described) for the title (or globally) and saves. 0 when it took.
int rpcs3ps5_cfg_set(const char *title_id, const char *path, const char *value);
// Applies config YAML (a recommended settings entry) on top of the title's settings and saves them as its own
int rpcs3ps5_cfg_apply_yaml(const char *title_id, const char *yaml);
// Whether the title has settings of its own
int rpcs3ps5_cfg_has_custom(const char *title_id);

#ifdef __cplusplus
}
#endif
