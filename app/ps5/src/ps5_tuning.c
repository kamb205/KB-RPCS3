/*
 * RPCS3 PS5 - live tuning knobs for the GPU wait paths (PORT_LOG: "stop spin-waiting on SPU cores' SMT
 * siblings"). The title defines them; RPCS3 (C++) and RADV (C) read them. A file on the console changes
 * them without a rebuild:
 *
 *   /app0/rpcs3/ps5-tuning.txt    one "key value" per line, '#' starts a comment, unknown keys are ignored
 *
 * There is no file by default: the built-in defaults equal the behaviour before this experiment, so
 * deleting the file (or never creating it) restores today's behaviour exactly.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#ifndef PS5_APP_ROOT
#define PS5_APP_ROOT "/app0"
#endif

#define TUNE_FILE PS5_APP_ROOT "/rpcs3/ps5-tuning.txt"

/* Must match the key numbers RPCS3 and RADV declare locally. */
enum {
	PS5_TUNE_RADV_SPIN_US = 0,
	PS5_TUNE_RADV_SLEEP_US,
	PS5_TUNE_VK_EVENT_SPIN_US,
	PS5_TUNE_VK_EVENT_SLEEP_US,
	PS5_TUNE_RSX_CPU,
	PS5_TUNE_SPU_INSERT_SELECT,
	PS5_TUNE_SPU_NARROW_LS_STORE,
	PS5_TUNE_SPU_HOT_CPU,
	PS5_TUNE_SPU_JIT_VERIFY,
	PS5_TUNE_SPU_NARROW_LS_CHECK,
	PS5_TUNE_COUNT
};

static const char *const g_names[PS5_TUNE_COUNT] = {
	"radv_spin_us", "radv_sleep_us", "vk_event_spin_us", "vk_event_sleep_us", "rsx_cpu",
	"spu_insert_select", "spu_narrow_ls_store", "spu_hot_cpu",
	"spu_jit_verify", "spu_narrow_ls_check" };

/* Defaults. r2: every SPU optimisation is OFF by default, so with no tuning file the build behaves like
 * the known-good build (spu_insert_select 0, spu_narrow_ls_store 0, spu_hot_cpu -1). Keys 8/9
 * (spu_jit_verify, spu_narrow_ls_check) are debug aids and are OFF too. vk_event_spin_us = -1 (unbounded
 * spin); rsx_cpu / spu_hot_cpu = -1 (no pinning) is the historic default. */
static const int64_t g_default[PS5_TUNE_COUNT] = { 4000, 100, -1, 100, -1, 0, 0, -1, 0, 0 };
static int64_t g_tune[PS5_TUNE_COUNT] = { 4000, 100, -1, 100, -1, 0, 0, -1, 0, 0 };

/* size ^ (mtime << 20) of the file last read; 0 means "no file seen" */
static uint64_t g_file_key;
static int g_describe_seq;
static char g_describe[512] = "radv_spin_us=4000 radv_sleep_us=100 vk_event_spin_us=-1 vk_event_sleep_us=100 rsx_cpu=-1 spu_insert_select=0 spu_narrow_ls_store=0 spu_hot_cpu=-1 spu_jit_verify=0 spu_narrow_ls_check=0";

/* Relaxed atomic load, cheap enough to call once per wait. */
int64_t rpcs3ps5_tune(int key)
{
	if (key < 0 || key >= PS5_TUNE_COUNT)
		return -1;
	return __atomic_load_n(&g_tune[key], __ATOMIC_RELAXED);
}

static int64_t tune_clamp(int key, int64_t v)
{
	switch (key) {
	case PS5_TUNE_RADV_SPIN_US:
	case PS5_TUNE_VK_EVENT_SPIN_US:
		if (v < 0)
			return -1; /* -1: unbounded spin (vk) / the default (radv) */
		return v > 10000 ? 10000 : v;
	case PS5_TUNE_RADV_SLEEP_US:
	case PS5_TUNE_VK_EVENT_SLEEP_US:
		return v < 1 ? 1 : (v > 2000 ? 2000 : v);
	case PS5_TUNE_RSX_CPU:
	case PS5_TUNE_SPU_HOT_CPU:
		return v < -1 ? -1 : (v > 12 ? 12 : v);
	case PS5_TUNE_SPU_INSERT_SELECT:
	case PS5_TUNE_SPU_NARROW_LS_STORE:
	case PS5_TUNE_SPU_JIT_VERIFY:
	case PS5_TUNE_SPU_NARROW_LS_CHECK:
		return v ? 1 : 0;
	default:
		return v;
	}
}

static void tune_store(int64_t *v)
{
	for (int i = 0; i < PS5_TUNE_COUNT; i++)
		__atomic_store_n(&g_tune[i], v[i], __ATOMIC_RELAXED);
}

static void tune_set_describe(const int64_t *v, const char *unknown)
{
	snprintf(g_describe, sizeof g_describe,
		"radv_spin_us=%lld radv_sleep_us=%lld vk_event_spin_us=%lld vk_event_sleep_us=%lld rsx_cpu=%lld "
		"spu_insert_select=%lld spu_narrow_ls_store=%lld spu_hot_cpu=%lld spu_jit_verify=%lld spu_narrow_ls_check=%lld%s%s",
		(long long)v[0], (long long)v[1], (long long)v[2], (long long)v[3], (long long)v[4],
		(long long)v[5], (long long)v[6], (long long)v[7], (long long)v[8], (long long)v[9],
		unknown && unknown[0] ? " unknown=" : "", unknown && unknown[0] ? unknown : "");
	__atomic_fetch_add(&g_describe_seq, 1, __ATOMIC_RELAXED);
}

/* Re-read the file if its size or mtime changed; delete it to restore the defaults. */
void rpcs3ps5_tune_refresh(void)
{
	struct stat st;
	if (stat(TUNE_FILE, &st) != 0) {
		int changed = 0;
		for (int i = 0; i < PS5_TUNE_COUNT; i++)
			if (__atomic_load_n(&g_tune[i], __ATOMIC_RELAXED) != g_default[i])
				changed = 1;
		if (changed) {
			tune_store((int64_t *)g_default);
			__atomic_store_n(&g_file_key, 0, __ATOMIC_RELAXED);
			tune_set_describe(g_default, NULL);
		}
		return;
	}

	const uint64_t key = (uint64_t)st.st_size ^ ((uint64_t)st.st_mtime << 20);
	if (key == __atomic_load_n(&g_file_key, __ATOMIC_RELAXED))
		return;
	__atomic_store_n(&g_file_key, key, __ATOMIC_RELAXED);

	int64_t v[PS5_TUNE_COUNT];
	for (int i = 0; i < PS5_TUNE_COUNT; i++)
		v[i] = g_default[i];
	char unknown[160] = "";

	FILE *f = fopen(TUNE_FILE, "r");
	if (f) {
		char line[256];
		while (fgets(line, sizeof line, f)) {
			char *hash = strchr(line, '#');
			if (hash)
				*hash = '\0';
			char k[64];
			long long val;
			if (sscanf(line, "%63s %lld", k, &val) != 2)
				continue;
			int found = -1;
			for (int i = 0; i < PS5_TUNE_COUNT; i++)
				if (!strcmp(k, g_names[i])) { found = i; break; }
			if (found < 0) {
				if (strlen(unknown) + strlen(k) + 2 < sizeof unknown) {
					if (unknown[0]) strncat(unknown, ",", sizeof unknown - strlen(unknown) - 1);
					strncat(unknown, k, sizeof unknown - strlen(unknown) - 1);
				}
				continue;
			}
			v[found] = tune_clamp(found, val);
		}
		fclose(f);
	}

	tune_store(v);
	tune_set_describe(v, unknown);
}

const char *rpcs3ps5_tune_describe(void)
{
	return g_describe;
}
