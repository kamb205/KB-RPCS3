/*
 * RPCS3 PS5 game tile launcher: the eboot of each PS3 game's own tile on the PS5 home screen.
 * Reads the game to boot from /app0/rpcs3-launch.txt (a path under RPCS3's data folder, as the RPCS3 PS5
 * library lists it) and has RPCS3 PS5 (PPSA99303) started with it.
 * A title may not start another title (only a system process may: SceLncService refuses with 0x8094000f),
 * so the tile sends the one-shot payload rpcs3ps5-launch.elf (tools/launcher/launch.c; its copy is in the
 * tile) to the ELF loader, with "<user>|<path>" written after the marker in its request buffer, and ends.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define LOADER_PORT 9021
#define MARKER "RPCS3PS5-LAUNCH-REQUEST:"

int sceUserServiceInitialize(void *params);
int sceUserServiceGetForegroundUser(int32_t *user_id);
int sceKernelDebugOutText(int channel, const char *text);
int sceSystemServiceLoadExec(const char *path, const char *const *argv);
int sceKernelUsleep(unsigned int microseconds);

// A title ends by asking the shell to close it: exit() is refused (SIGSYS) and reported as a crash
// ("something went wrong"). The CRT calls this after main returns.
void catchReturnFromMain(int status)
{
	(void)status;
	sceSystemServiceLoadExec("exit", NULL);
	for (;;)
		sceKernelUsleep(100000);
}

static void say(const char *text)
{
	char line[900];
	snprintf(line, sizeof(line), "[RPCS3 tile] %s\n", text);
	sceKernelDebugOutText(0, line);
}

static int send_all(int s, const void *data, size_t size)
{
	const char *p = data;
	while (size) {
		const ssize_t n = send(s, p, size, 0);
		if (n <= 0)
			return -1;
		p += n;
		size -= (size_t)n;
	}
	return 0;
}

// The launch payload, with the request written in, to the ELF loader
static int send_launch(const char *request)
{
	FILE *f = fopen("/app0/rpcs3ps5-launch.elf", "rb");
	if (!f)
		return -1;
	fseek(f, 0, SEEK_END);
	const long size = ftell(f);
	fseek(f, 0, SEEK_SET);
	char *elf = malloc((size_t)size);
	const int ok = elf && fread(elf, 1, (size_t)size, f) == (size_t)size;
	fclose(f);
	char *slot = NULL;
	for (long i = 0; ok && i + (long)sizeof(MARKER) < size && !slot; i++)
		if (!memcmp(elf + i, MARKER, sizeof(MARKER) - 1))
			slot = elf + i + sizeof(MARKER) - 1;
	// the buffer is 1024 bytes including the marker
	if (!slot || strlen(request) + 1 > 1024 - (sizeof(MARKER) - 1)) {
		free(elf);
		return -2;
	}
	memcpy(slot, request, strlen(request) + 1);

	const int s = socket(AF_INET, SOCK_STREAM, 0);
	struct sockaddr_in a;
	memset(&a, 0, sizeof(a));
	a.sin_family = AF_INET;
	a.sin_port = htons(LOADER_PORT);
	a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	int r = -3;
	if (s >= 0 && !connect(s, (struct sockaddr *)&a, sizeof(a)))
		r = send_all(s, elf, (size_t)size) ? -4 : 0;
	if (s >= 0)
		close(s);
	free(elf);
	return r;
}

int main(int argc, char **argv)
{
	(void)argc;
	(void)argv;
	char game[700] = {0};
	FILE *f = fopen("/app0/rpcs3-launch.txt", "r");
	if (!f || !fgets(game, sizeof(game), f)) {
		say("no /app0/rpcs3-launch.txt");
		return 0;
	}
	fclose(f);
	game[strcspn(game, "\r\n")] = 0;

	int32_t user = -1;
	sceUserServiceInitialize(NULL);
	sceUserServiceGetForegroundUser(&user);

	char request[800], msg[900];
	snprintf(request, sizeof(request), "%d|%s", (int)user, game);
	const int r = send_launch(request);
	snprintf(msg, sizeof(msg), "launch payload for RPCS3 PS5 '%s' (user %d) sent: %d", game, (int)user, r);
	say(msg);
	return 0; // the payload starts RPCS3 PS5 once this tile has ended
}
