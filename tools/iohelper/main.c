// rpcs3ps5-io: write helper for the RPCS3 PS5 title (personal use).
//
// The PS5 throttles file writes made by the title's own process (game data installs fell from 33 MB/s
// to ~0.5 MB/s after ~1 GB), while an unsandboxed payload writes the same folder at a steady ~42 MB/s.
// The title starts this payload through the ELF loader (:9021) and sends it its writes over
// 127.0.0.1:9078. Only files under ROOT are accepted, and the file must be the one the title has open
// (same inode), so a deleted/recreated file is never written by mistake.
// The game creates its files owner-only (0600) as the title's user: the helper raises its own
// privileges at start and, if a file still refuses, takes the file owner's uid.
// A request whose path is "\x01quit" ends the helper (lets a newer version replace it).
// Version 2/3 (2026-10-07) add, for the title's PS5 home-screen tiles and "Delete game":
//   "\x04version"            reply: VERSION (version 1 answers with an error)
//   "\x02tile/<ID>/<file>"   write data at offset into /data/homebrew/<ID>/<file> (ID = PPSA8nnnn, a
//                            tile's title ID); offset 0 creates the file (and its folders) afresh
//   "\x03rmtile/<ID>"        delete that tile's folder (the icon: Options > Delete on the PS5 menu)
//   "\x03rm/<path>"          delete <path> under ROOT (file or folder, recursively); only inside games/,
//                            dev_hdd0/game/, dev_hdd0/home/<user>/savedata/, cache/ and custom_configs/
//   "\x06get/<file>|<path>"   HTTP GET http://art.gametdb.com<path> into ROOT covers/<file> (the title's
//                            sandbox has no network); reply: bytes saved, or -HTTP status / -errno
//
// Request:  u32 magic 'R3IO', u32 path_len, u64 inode, u64 offset, u64 size, path bytes (relative to
//           ROOT, no "..") then size data bytes.
// Reply:    s64 bytes written, or -errno.
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <dirent.h>
#include <netdb.h>
#include <sys/stat.h>
#include <unistd.h>

#include <ps5/kernel.h>

#define ROOT "/data/homebrew/PPSA99303/rpcs3/"
#define PORT 9078 // 9077: first version (could not open the game's 0600 files)
#define SPARE_PORT 9079 // when 9078 is held by a helper stuck in a system call (2026-10-07: until reboot)
#define MAGIC 0x4f493352u // "R3IO"
#define CACHE 8
#define VERSION 9
#define TILES "/data/homebrew/"

int sceKernelLoadStartModule(const char* path, size_t args, const void* argp, uint32_t flags, void* opt, int* res);
int sceKernelDlsym(int handle, const char* symbol, void** address);


struct request
{
	uint32_t magic;
	uint32_t path_len;
	uint64_t inode;
	uint64_t offset;
	uint64_t size;
} __attribute__((packed));

struct entry
{
	char path[1024];
	uint64_t inode;
	int fd;
	unsigned long used;
};

static struct entry g_cache[CACHE];
static unsigned long g_tick;
static char g_buf[4 << 20];

static int read_all(int s, void* p, size_t n)
{
	char* c = p;
	while (n)
	{
		ssize_t r = read(s, c, n);
		if (r <= 0)
			return -1;
		c += r;
		n -= (size_t)r;
	}
	return 0;
}

static int write_all(int s, const void* p, size_t n)
{
	const char* c = p;
	while (n)
	{
		ssize_t r = write(s, c, n);
		if (r <= 0)
			return -1;
		c += r;
		n -= (size_t)r;
	}
	return 0;
}

static void raise_privileges(void)
{
	static const uint8_t caps[16] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
	kernel_set_ucred_authid(getpid(), 0x4801000000000013l);
	kernel_set_ucred_caps(getpid(), caps);
	kernel_set_ucred_uid(getpid(), 0);
	kernel_set_ucred_ruid(getpid(), 0);
	kernel_set_ucred_svuid(getpid(), 0);
}

// A tile's title ID: PPSA8 and four digits (never another title's folder)
static int is_tile_id(const char* id, size_t n)
{
	if (n != 9 || strncmp(id, "PPSA8", 5))
		return 0;
	for (int i = 5; i < 9; i++)
		if (id[i] < '0' || id[i] > '9')
			return 0;
	return 1;
}

static int mkdirs_for(char* path)
{
	for (char* p = path + 1; *p; p++)
	{
		if (*p != '/')
			continue;
		*p = 0;
		if (mkdir(path, 0777) && errno != EEXIST)
		{
			*p = '/';
			return -errno;
		}
		chmod(path, 0777);
		*p = '/';
	}
	return 0;
}

static int remove_tree(const char* path)
{
	struct stat st;
	if (lstat(path, &st))
		return errno == ENOENT ? 0 : -errno;
	if (S_ISDIR(st.st_mode))
	{
		DIR* d = opendir(path);
		if (!d)
			return -errno;
		struct dirent* e;
		char child[1024];
		int r = 0;
		while ((e = readdir(d)))
		{
			if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, ".."))
				continue;
			snprintf(child, sizeof(child), "%s/%s", path, e->d_name);
			const int c = remove_tree(child);
			if (c < 0 && !r)
				r = c;
		}
		closedir(d);
		if (rmdir(path) && !r)
			r = -errno;
		return r;
	}
	return unlink(path) ? -errno : 0;
}

// Cached descriptors of files about to be deleted are closed first
static void forget_under(const char* path)
{
	const size_t n = strlen(path);
	for (int i = 0; i < CACHE; i++)
		if (g_cache[i].fd > 0 && !strncmp(g_cache[i].path, path, n))
		{
			close(g_cache[i].fd);
			g_cache[i].fd = 0;
		}
}

// What "\x03rm/" may delete: something below one of these folders (never the folder itself)
static int may_delete(const char* rel)
{
	static const char* const kAreas[] = {"games/", "dev_hdd0/game/", "cache/", "custom_configs/", NULL};
	if (!*rel || strstr(rel, "..") || rel[0] == '/' || strstr(rel, "//"))
		return 0;
	for (int i = 0; kAreas[i]; i++)
	{
		const size_t n = strlen(kAreas[i]);
		if (!strncmp(rel, kAreas[i], n) && rel[n] && rel[n] != '/')
			return 1;
	}
	// dev_hdd0/home/<user>/savedata/<save>
	if (!strncmp(rel, "dev_hdd0/home/", 14))
	{
		const char* save = strstr(rel + 14, "/savedata/");
		return save && !memchr(rel + 14, '/', (size_t)(save - (rel + 14))) && save[10] && !strchr(save + 10, '/');
	}
	return 0;
}

// GameTDB's box art, for the PS5 home-screen tiles: http://art.gametdb.com<path> into ROOT covers/<file>
static int64_t fetch_cover(const char* file, const char* path)
{
	if (!*file || strchr(file, '/') || strstr(file, "..") || path[0] != '/' || strstr(path, "..") || strpbrk(path, " \r\n"))
		return -EPERM;
	struct addrinfo hints, *found = NULL;
	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;
	if (getaddrinfo("art.gametdb.com", "80", &hints, &found) || !found)
		return -EHOSTUNREACH;
	const int s = socket(AF_INET, SOCK_STREAM, 0);
	struct timeval timeout = {15, 0};
	setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
	setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
	const int connected = s >= 0 && !connect(s, found->ai_addr, found->ai_addrlen);
	freeaddrinfo(found);
	if (!connected)
	{
		if (s >= 0)
			close(s);
		return -ECONNREFUSED;
	}
	char request[1200];
	snprintf(request, sizeof(request),
	         "GET %s HTTP/1.0\r\nHost: art.gametdb.com\r\nUser-Agent: RPCS3-PS5\r\nConnection: close\r\n\r\n", path);
	size_t used = 0;
	if (!write_all(s, request, strlen(request)))
	{
		ssize_t n;
		while (used < sizeof(g_buf) && (n = read(s, g_buf + used, sizeof(g_buf) - used)) > 0)
			used += (size_t)n;
	}
	close(s);
	int status = 0;
	char* body = NULL;
	for (size_t i = 0; i + 4 <= used && !body; i++)
		if (!memcmp(g_buf + i, "\r\n\r\n", 4))
			body = g_buf + i;
	if (!body || sscanf(g_buf, "HTTP/%*d.%*d %d", &status) != 1)
		return -EIO;
	if (status != 200)
		return -status;
	body += 4;
	const size_t size = used - (size_t)(body - g_buf);
	char dest[1024];
	snprintf(dest, sizeof(dest), ROOT "covers");
	mkdir(dest, 0777);
	chmod(dest, 0777);
	snprintf(dest, sizeof(dest), ROOT "covers/%s", file);
	const int fd = open(dest, O_WRONLY | O_CREAT | O_TRUNC, 0666);
	if (fd < 0)
		return -errno;
	fchmod(fd, 0666);
	const int ok = write_all(fd, body, size) == 0;
	close(fd);
	return ok ? (int64_t)size : -EIO;
}

// The version 2 commands; -ENOSYS when rel is none of them
static int64_t command(const char* rel, const struct request* q)
{
	char path[1024];
	if (!strcmp(rel, "\x04version"))
		return VERSION;
	if (!strncmp(rel, "\x02tile/", 6))
	{
		const char* id = rel + 6;
		const char* file = strchr(id, '/');
		if (!file || !is_tile_id(id, (size_t)(file - id)) || !file[1] || strstr(file, ".."))
			return -EPERM;
		raise_privileges();
		snprintf(path, sizeof(path), TILES "%s", id);
		int fd;
		if (q->offset == 0)
		{
			const int m = mkdirs_for(path);
			if (m < 0)
				return m;
			unlink(path);
			fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0777);
		}
		else
		{
			fd = open(path, O_WRONLY);
		}
		if (fd < 0)
			return -errno;
		// Executable like a title's own files: without it the tile's eboot.bin is refused (spawn errno 13)
		fchmod(fd, 0777);
		const ssize_t w = pwrite(fd, g_buf, q->size, (off_t)q->offset);
		const int64_t result = w < 0 ? -errno : w;
		close(fd);
		return result;
	}
	if (!strncmp(rel, "\x06get/", 5))
	{
		char file[256];
		const char* bar = strchr(rel + 5, '|');
		if (!bar || (size_t)(bar - (rel + 5)) >= sizeof(file))
			return -EPERM;
		memcpy(file, rel + 5, (size_t)(bar - (rel + 5)));
		file[bar - (rel + 5)] = 0;
		return fetch_cover(file, bar + 1);
	}
	if (!strncmp(rel, "\x03rmtile/", 8))
	{
		const char* id = rel + 8;
		if (!is_tile_id(id, strlen(id)))
			return -EPERM;
		raise_privileges();
		snprintf(path, sizeof(path), TILES "%s", id);
		// Files only: the uninstall call (libSceAppInstUtil, loaded at run time) hung the helper for good
		// (2026-10-07); the icon goes with the PS5 menu's own Options > Delete
		const int r = remove_tree(path);
		printf("rpcs3ps5-io: tile %s removed (%d)\n", id, r);
		return r;
	}
	if (!strncmp(rel, "\x03rm/", 4))
	{
		if (!may_delete(rel + 4))
			return -EPERM;
		raise_privileges();
		snprintf(path, sizeof(path), ROOT "%s", rel + 4);
		forget_under(path);
		const int r = remove_tree(path);
		printf("rpcs3ps5-io: deleted %s (%d)\n", path, r);
		return r;
	}
	return -ENOSYS;
}

// An fd for path whose inode is `inode`, from the cache or freshly opened; -errno on failure
static int file_for(const char* path, uint64_t inode)
{
	struct entry* victim = &g_cache[0];
	for (int i = 0; i < CACHE; i++)
	{
		struct entry* e = &g_cache[i];
		if (e->fd > 0 && e->inode == inode && !strcmp(e->path, path))
		{
			e->used = ++g_tick;
			return e->fd;
		}
		if (e->used < victim->used)
			victim = e;
	}
	int fd = open(path, O_WRONLY);
	if (fd < 0 && errno == EACCES)
	{
		struct stat owner;
		if (!stat(path, &owner))
		{
			kernel_set_ucred_uid(getpid(), owner.st_uid);
			kernel_set_ucred_ruid(getpid(), owner.st_uid);
			kernel_set_ucred_svuid(getpid(), owner.st_uid);
			fd = open(path, O_WRONLY);
			if (fd >= 0)
				printf("rpcs3ps5-io: writing as uid %u\n", (unsigned)owner.st_uid);
		}
	}
	if (fd < 0)
		return -errno;
	struct stat st;
	if (fstat(fd, &st) || (uint64_t)st.st_ino != inode)
	{
		close(fd);
		return -ESTALE;
	}
	if (victim->fd > 0)
		close(victim->fd);
	snprintf(victim->path, sizeof(victim->path), "%s", path);
	victim->inode = inode;
	victim->fd = fd;
	victim->used = ++g_tick;
	return fd;
}

static void serve(int s)
{
	struct request q;
	char rel[900], path[1024];
	while (!read_all(s, &q, sizeof(q)))
	{
		int64_t result;
		if (q.magic != MAGIC || q.path_len == 0 || q.path_len >= sizeof(rel) || q.size > sizeof(g_buf))
			return;
		if (read_all(s, rel, q.path_len) || read_all(s, g_buf, q.size))
			return;
		rel[q.path_len] = 0;
		if (!strcmp(rel, "\x01quit"))
		{
			printf("rpcs3ps5-io: quit requested\n");
			exit(0);
		}
		if ((result = command(rel, &q)) != -ENOSYS)
		{
			// a version 2 command
		}
		else if (strstr(rel, "..") || rel[0] == '/')
		{
			result = -EPERM;
		}
		else
		{
			snprintf(path, sizeof(path), ROOT "%s", rel);
			int fd = file_for(path, q.inode);
			if (fd < 0)
			{
				result = fd;
			}
			else
			{
				ssize_t w = pwrite(fd, g_buf, q.size, (off_t)q.offset);
				result = w < 0 ? -errno : w;
			}
		}
		if (write_all(s, &result, sizeof(result)))
			return;
	}
}

int main(void)
{
	// Full privileges for this process (the game's files are 0600, owned by the title's user)
	raise_privileges();
	// Its output reaches no log: a line in the data folder says it started and whether the port was free
	FILE* status = fopen(ROOT "iohelper.txt", "w");

	int ls = socket(AF_INET, SOCK_STREAM, 0);
	int one = 1;
	setsockopt(ls, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
	struct sockaddr_in a;
	memset(&a, 0, sizeof(a));
	a.sin_family = AF_INET;
	a.sin_port = htons(PORT);
	a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	int port = PORT;
	if (bind(ls, (struct sockaddr*)&a, sizeof(a)))
	{
		port = SPARE_PORT;
		a.sin_port = htons(SPARE_PORT);
	}
	if ((port == SPARE_PORT && bind(ls, (struct sockaddr*)&a, sizeof(a))) || listen(ls, 4))
	{
		printf("rpcs3ps5-io: port %d busy (already running?)\n", PORT);
		if (status)
		{
			fprintf(status, "version %d: port %d busy (errno %d)\n", VERSION, PORT, errno);
			fclose(status);
		}
		return 1;
	}
	if (status)
	{
		fprintf(status, "version %d: listening on 127.0.0.1:%d (pid %d)\n", VERSION, port, getpid());
		fclose(status);
	}
	printf("rpcs3ps5-io: version %d listening on 127.0.0.1:%d for %s\n", VERSION, port, ROOT);
	for (;;)
	{
		int s = accept(ls, NULL, NULL);
		if (s < 0)
			continue;
		setsockopt(s, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
		serve(s);
		close(s);
		// files stay cached across connections; the inode check guards reuse
	}
}
