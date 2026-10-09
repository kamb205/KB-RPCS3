/*
 * rpcs3ps5-launch: one-shot payload a game tile sends to the ELF loader to start RPCS3 PS5 with its game.
 * A title may not start another title (SceLncService: only a system process may), so this waits for the
 * tile to end, then calls sceLncUtilInitialize + sceLncUtilLaunchApp("PPSA99303", {"--boot", path}) (the
 * console passes those arguments on to main()), and ends.
 * sceLncUtilLaunchApp starts the title but may never return: this process then just lingers, holding nothing
 * (the write helper did this on 2026-10-07 and kept its port busy until the console restarted).
 * The tile writes "<user>|<path>" into g_request (after the marker) in its copy of this ELF before sending it;
 * tools/ps5-launch.sh sends user -1 (= the foreground user, asked here).
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <ps5/kernel.h>

#define ROOT "/data/homebrew/PPSA99303/rpcs3/"

// Patched by the tile: the marker stays, "<user>|<path>" follows it
volatile char g_request[1024] = "RPCS3PS5-LAUNCH-REQUEST:";

typedef struct
{
	uint32_t size;
	int32_t user_id;
	uint32_t app_opt;
	uint64_t crash_report;
	uint32_t check_flag;
} LncAppParam;

// Linked (-lSceSystemService): loading the module at run time with sceKernelLoadStartModule hung (2026-10-07)
int sceLncUtilInitialize(void);
int sceLncUtilLaunchApp(const char* title_id, const char** argv, LncAppParam* param);
int sceUserServiceInitialize(void* params);
int sceUserServiceGetForegroundUser(int32_t* user_id);

// One line appended to rpcs3/iolaunch.txt (fopen's "a" mode writes nothing from a payload: open() it is)
static void note(const char* text, unsigned value)
{
	char line[200];
	const int n = snprintf(line, sizeof(line), "%s 0x%08x\n", text, value);
	const int fd = open(ROOT "iolaunch.txt", O_WRONLY | O_CREAT | O_APPEND, 0666);
	if (fd >= 0)
	{
		write(fd, line, (size_t)n);
		close(fd);
	}
}

int main(void)
{
	static const uint8_t caps[16] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
	kernel_set_ucred_authid(getpid(), 0x4801000000000013l);
	kernel_set_ucred_caps(getpid(), caps);
	kernel_set_ucred_uid(getpid(), 0);

	char request[1024];
	for (size_t i = 0; i < sizeof(request); i++)
		request[i] = g_request[i];
	request[sizeof(request) - 1] = 0;
	const char* body = request + strlen("RPCS3PS5-LAUNCH-REQUEST:");
	const char* bar = strchr(body, '|');
	if (!bar || strncmp(bar + 1, "/app0/rpcs3/", 12) || strstr(bar + 1, ".."))
	{
		note("launch: no valid request", 0);
		return 1;
	}
	int user = atoi(body);
	const char* path = bar + 1;
	if (user == -1)
	{
		// Sent from the Mac (tools/ps5-launch.sh), not a tile: the user signed in in front of the console
		int32_t fg = -1;
		sceUserServiceInitialize(NULL);
		sceUserServiceGetForegroundUser(&fg);
		user = fg;
		sleep(1); // no tile to wait for below, but harmless
	}

	note("launch: request for user", (unsigned)user);
	sleep(2); // the tile ends first
	int (*initialize)(void) = sceLncUtilInitialize;
	int (*launch)(const char*, const char**, LncAppParam*) = sceLncUtilLaunchApp;
	// The launch service's client must be set up first: without it the first launch started the title
	// but never returned, and later ones then waited behind it (2026-10-07)
	note("launch: sceLncUtilInitialize", (unsigned)initialize());
	note("launch: starting PPSA99303 for user", (unsigned)user);
	const char* argv[] = {"--boot", path, NULL};
	LncAppParam param = {sizeof(LncAppParam), user, 0, 0, 0};
	const int r = launch("PPSA99303", argv, &param);
	note("launch: sceLncUtilLaunchApp returned", (unsigned)r);
	return 0;
}
