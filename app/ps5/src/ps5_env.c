/*
 * RPCS3 PS5 - the environment (getenv, setenv, unsetenv, putenv) in the title itself.
 *
 * A title's import of getenv is left pointing at nothing on the console (no module
 * exports it to titles), and RPCS3 calls getenv from static constructors. These are
 * bound in place of the system's by tools/link-title.sh (--defsym, kept local), and
 * work from the first constructor on: a fixed table, no allocation, a spin lock.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <stdatomic.h>
#include <stddef.h>
#include <string.h>

#define ENV_MAX 64
#define ENV_ENTRY 512

static char g_env[ENV_MAX][ENV_ENTRY]; /* "NAME=value" */
static atomic_flag g_lock = ATOMIC_FLAG_INIT;

static void lock(void)
{
    while (atomic_flag_test_and_set_explicit(&g_lock, memory_order_acquire))
        ;
}

static void unlock(void)
{
    atomic_flag_clear_explicit(&g_lock, memory_order_release);
}

static int find(const char *name, size_t length)
{
    for (int i = 0; i < ENV_MAX; i++)
        if (g_env[i][0] && strncmp(g_env[i], name, length) == 0 && g_env[i][length] == '=')
            return i;
    return -1;
}

char *rpcs3ps5_getenv(const char *name)
{
    if (!name || !*name || strchr(name, '='))
        return NULL;
    lock();
    const int at = find(name, strlen(name));
    char *value = at >= 0 ? g_env[at] + strlen(name) + 1 : NULL;
    unlock();
    return value;
}

int rpcs3ps5_setenv(const char *name, const char *value, int overwrite)
{
    if (!name || !*name || strchr(name, '='))
        return -1;
    if (!value)
        value = "";
    const size_t length = strlen(name);
    if (length + 1 + strlen(value) + 1 > ENV_ENTRY)
        return -1;
    lock();
    int at = find(name, length);
    if (at >= 0 && !overwrite) {
        unlock();
        return 0;
    }
    if (at < 0)
        for (int i = 0; i < ENV_MAX && at < 0; i++)
            if (!g_env[i][0])
                at = i;
    if (at < 0) {
        unlock();
        return -1;
    }
    memcpy(g_env[at], name, length);
    g_env[at][length] = '=';
    strcpy(g_env[at] + length + 1, value);
    unlock();
    return 0;
}

int rpcs3ps5_unsetenv(const char *name)
{
    if (!name || !*name || strchr(name, '='))
        return -1;
    lock();
    const int at = find(name, strlen(name));
    if (at >= 0)
        g_env[at][0] = 0;
    unlock();
    return 0;
}

int rpcs3ps5_putenv(char *assignment)
{
    const char *equals = assignment ? strchr(assignment, '=') : NULL;
    if (!equals || equals == assignment)
        return -1;
    char name[ENV_ENTRY];
    const size_t length = (size_t)(equals - assignment);
    if (length >= sizeof(name))
        return -1;
    memcpy(name, assignment, length);
    name[length] = 0;
    return rpcs3ps5_setenv(name, equals + 1, 1);
}

/* cpuset_getaffinity, which LLVM's thread count reads on FreeBSD (RPCS3's JIT sizes its
 * pools with it). No system module gives it to a title; the CPUs the title may run on are
 * the calling thread's affinity, which the platform layer reads through
 * scePthreadGetaffinity (ps5platform/libc.h). */
#include <pthread.h>
int ps5_pthread_getaffinity_np(pthread_t thread, size_t size, void *set);

int rpcs3ps5_cpuset_getaffinity(int level, int which, long long id, size_t size, void *mask)
{
    (void)level;
    (void)which;
    (void)id;
    const int error = ps5_pthread_getaffinity_np(pthread_self(), size, mask);
    if (error) {
        extern int *__error(void);
        *__error() = error;
        return -1;
    }
    return 0;
}

/* pathconf, which no system module gives a title (ps5-boot.log, console run 4): the limits
 * of the console's file systems as FreeBSD reports them; anything else is unknown. */
#include <unistd.h>
#include <errno.h>
long rpcs3ps5_pathconf(const char *path, int name)
{
    (void)path;
    switch (name) {
    case _PC_NAME_MAX: return 255;
    case _PC_PATH_MAX: return 1024;
    case _PC_LINK_MAX: return 32767;
    case _PC_PIPE_BUF: return 512;
    default:
        errno = EINVAL;
        return -1;
    }
}
