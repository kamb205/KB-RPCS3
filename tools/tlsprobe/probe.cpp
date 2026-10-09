/*
 * TLS probe title (PPSA99391 E/F, PPSA99392 N).
 *
 * Built with the same SDK, compiler wrappers and link recipe as the RPCS3 title. Writes its findings to
 * /app0/tlsprobe-<variant>.txt and ends with sceSystemServiceLoadExec("exit").
 *
 *   PROBE_EF: emulated TLS, stock __emutls_get_address (E) + ps5_fast_emutls_get_address (F).
 *   PROBE_N : native TLS (initial-exec/local-exec), PT_TLS linker-script change.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "emutls_control.h"

#include <atomic>
#include <cinttypes>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <pthread.h>
#include <signal.h>
#include <thread>
#include <unistd.h>
#include <vector>

extern "C" {
int sceKernelDebugOutText(int channel, const char* text);
int sceSystemServiceLoadExec(const char* path, const char* const* argv);
int sceKernelUsleep(unsigned int usec);
void ps5_pthread_exit(void* value) __attribute__((noreturn));
struct ps5_heap_stats { uint64_t unknown[8]; };
void ps5_heap_stats(struct ps5_heap_stats* stats);
void ps5_fp_ieee(void);
unsigned ps5_fast_emutls_cache_live(void);
}

#ifndef PROBE_TAG
#define PROBE_TAG "EF"
#endif
#ifndef SPEED_ITERS
#define SPEED_ITERS 100000000ull
#endif
static constexpr unsigned long long kMagic = 0x5EC0DE00A11CE5EDull;

// ---- thread_local test variables (extern "C" so the __emutls_v.* control names are predictable) ----
extern "C" {
thread_local int g_a = 42;
thread_local unsigned long long g_magic = kMagic;
thread_local unsigned long long g_z; /* zero-initialised */
}

static std::atomic<int> g_ctor_total{0};
static std::atomic<int> g_dtor_total{0};
struct Tracked
{
	unsigned char pad[24];
	Tracked() { g_ctor_total.fetch_add(1, std::memory_order_relaxed); }
	~Tracked() { g_dtor_total.fetch_add(1, std::memory_order_relaxed); }
};
extern "C" { thread_local Tracked g_tracked; }

#ifdef PROBE_EF
// F is tested on controls of its own, never shared with the stock implementation: E and F keep independent
// index allocators, so pointing both at one control (the old invalid design) is not done. A manual control
// has exactly the layout the compiler emits for a thread_local.
static int g_e_init = 5;
static int g_f_init = 7;
static __emutls_control g_e_ctl = { sizeof(int), alignof(int), {0}, &g_e_init };
static __emutls_control g_f_ctl = { sizeof(int), alignof(int), {0}, &g_f_init };
#endif

// ---- logging ----
static FILE* g_log;

static void logf(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
static void logf(const char* fmt, ...)
{
	char line[512];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(line, sizeof line, fmt, ap);
	va_end(ap);
	if (g_log)
	{
		fputs(line, g_log);
		fputc('\n', g_log);
		fflush(g_log);
	}
	char out[600];
	snprintf(out, sizeof out, "[tlsprobe-%s] %s\n", PROBE_TAG, line);
	sceKernelDebugOutText(0, out);
}

// ---- per-thread checks ----
static std::atomic<int> g_threads_done{0};
static std::atomic<int> g_threads_bad{0};
static pthread_mutex_t g_addr_mutex = PTHREAD_MUTEX_INITIALIZER;
static std::vector<uintptr_t> g_addr_a;
static std::vector<uintptr_t> g_addr_magic;

struct ThreadResult { bool init_ok; int a_after; uintptr_t addr_a, addr_magic; };

static ThreadResult check_thread(bool hammer)
{
	ThreadResult r{};
	r.init_ok = (g_a == 42) && (g_magic == kMagic) && (g_z == 0);
	r.addr_a = (uintptr_t)&g_a;
	r.addr_magic = (uintptr_t)&g_magic;
	if (hammer)
		for (int i = 0; i < 1000000; i++)
			g_a++;
	r.a_after = g_a;
	g_tracked.pad[0] = (unsigned char)g_a; /* force g_tracked to be instantiated per thread */
	return r;
}

static void record(ThreadResult r)
{
	if (!r.init_ok || r.a_after != 42 + 1000000)
		g_threads_bad.fetch_add(1, std::memory_order_relaxed);
	pthread_mutex_lock(&g_addr_mutex);
	g_addr_a.push_back(r.addr_a);
	g_addr_magic.push_back(r.addr_magic);
	pthread_mutex_unlock(&g_addr_mutex);
	g_threads_done.fetch_add(1, std::memory_order_relaxed);
}

static void* pthread_worker(void*)
{
	record(check_thread(true));
	return nullptr;
}

static std::atomic<int> g_reuse_bad{0};
static void* pthread_fresh_worker(void*)
{
	if (!(g_a == 42 && g_magic == kMagic && g_z == 0))
		g_reuse_bad.fetch_add(1, std::memory_order_relaxed);
	g_tracked.pad[0] = 1;
	return nullptr;
}

static void* pthread_exit_worker(void*)
{
	check_thread(true);
	g_threads_done.fetch_add(1, std::memory_order_relaxed);
	pthread_exit(nullptr);
}

static void* ps5_exit_worker(void*)
{
	check_thread(true);
	g_threads_done.fetch_add(1, std::memory_order_relaxed);
	ps5_pthread_exit(nullptr);
}

static std::atomic<int> g_signal_ok{0};
static void sig_handler(int, siginfo_t*, void*)
{
	/* read and write a thread_local from a signal handler on this thread */
	if (g_a >= 42 && g_magic == kMagic)
	{
		const int before = g_a;
		g_a = before + 1;
		if (g_a == before + 1)
			g_signal_ok.fetch_add(1, std::memory_order_relaxed);
		g_a = before;
	}
}

static void run_signal_test()
{
	struct sigaction sa{};
	sa.sa_sigaction = sig_handler;
	sa.sa_flags = SA_SIGINFO | SA_RESTART;
	sigaction(SIGUSR1, &sa, nullptr);
	g_signal_ok = 0;
	/* signal to self from the main thread */
	raise(SIGUSR1);
	std::thread t([] { raise(SIGUSR1); });
	t.join();
	logf("signal: %d/2 handlers saw the thread's own thread_local values", g_signal_ok.load());
}

static void run_common()
{
	logf("variant %s, %s", PROBE_TAG,
#ifdef PROBE_N
		"native TLS"
#else
		"emulated TLS"
#endif
	);
	logf("main: a=%d magic=0x%llx z=%llu", g_a, g_magic, g_z);

	// 8 std::thread
	std::vector<std::thread> ts;
	for (int i = 0; i < 8; i++)
		ts.emplace_back([] { record(check_thread(true)); });
	for (auto& t : ts) t.join();
	// 8 raw pthread_create
	pthread_t pt[8];
	for (int i = 0; i < 8; i++) pthread_create(&pt[i], nullptr, pthread_worker, nullptr);
	for (int i = 0; i < 8; i++) pthread_join(pt[i], nullptr);

	logf("threads: %d done, %d bad (initial values/isolation)", g_threads_done.load(), g_threads_bad.load());
	logf("main unchanged: a=%d magic=0x%llx z=%llu", g_a, g_magic, g_z);

	// distinct addresses
	bool distinct = true;
	for (size_t i = 1; i < g_addr_a.size(); i++)
		if (g_addr_a[i] == g_addr_a[0] || g_addr_magic[i] == g_addr_magic[0]) distinct = false;
	logf("addresses: %zu threads, distinct=%s (first a=0x%" PRIxPTR " magic=0x%" PRIxPTR ")",
		g_addr_a.size(), distinct ? "yes" : "NO", g_addr_a.empty() ? 0 : g_addr_a[0],
		g_addr_magic.empty() ? 0 : g_addr_magic[0]);

	// reuse: 2000 sequential create/join; every new thread must see fresh initial values
	for (int i = 0; i < 2000; i++)
	{
		pthread_t t;
		pthread_create(&t, nullptr, pthread_fresh_worker, nullptr);
		pthread_join(t, nullptr);
	}
	logf("reuse: 2000 create/join done, %d threads saw stale initial values", g_reuse_bad.load());

	run_signal_test();
}

static void run_destructors()
{
	g_ctor_total = 0;
	g_dtor_total = 0;
	{ std::thread t([] { volatile auto* p = &g_tracked; (void)p; }); t.join(); }
	const int after_std = g_dtor_total.load();
	{ pthread_t t; pthread_create(&t, nullptr, [](void*) -> void* { volatile auto* p = &g_tracked; (void)p; return nullptr; }, nullptr); pthread_join(t, nullptr); }
	const int after_ret = g_dtor_total.load();
	{ pthread_t t; pthread_create(&t, nullptr, pthread_exit_worker, nullptr); pthread_join(t, nullptr); }
	const int after_pexit = g_dtor_total.load();
	{ pthread_t t; pthread_create(&t, nullptr, ps5_exit_worker, nullptr); pthread_join(t, nullptr); }
	const int after_ps5 = g_dtor_total.load();
	logf("destructors: ctors=%d | dtor totals: std::thread=%d, return=%d, pthread_exit=%d, ps5_pthread_exit=%d",
		g_ctor_total.load(), after_std, after_ret, after_pexit, after_ps5);
}

static volatile unsigned long long g_sink;
static volatile int g_plain; // non-TLS global

static unsigned long long now_ns()
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (unsigned long long)ts.tv_sec * 1000000000ull + (unsigned long long)ts.tv_nsec;
}

#ifdef PROBE_EF
static void run_f_test()
{
	// F on its own control: initial value, per-thread isolation, and cache lifetime across threads.
	int* p = (int*)ps5_fast_emutls_get_address(&g_f_ctl);
	logf("F: main object value=%d (want 7) at %p; cache live entries=%u",
		*p, (void*)p, ps5_fast_emutls_cache_live());
	std::atomic<int> bad{0};
	std::vector<std::thread> ts;
	for (int i = 0; i < 8; i++)
		ts.emplace_back([&] {
			int* q = (int*)ps5_fast_emutls_get_address(&g_f_ctl);
			if (*q != 7 || q == p) bad.fetch_add(1, std::memory_order_relaxed);
			for (int k = 0; k < 100000; k++) (*q)++;
		});
	for (auto& t : ts) t.join();
	// The 8 exited threads' entries must be gone; only the main thread's remains.
	logf("F: 8 threads initial value/distinct: %d bad; cache live entries=%u (want 1: main only)",
		bad.load(), ps5_fast_emutls_cache_live());
}

static void speed_ef()
{
	int p = 0;
	unsigned long long t0 = now_ns();
	for (unsigned long long i = 0; i < SPEED_ITERS; i++) { g_plain = (int)i; p += g_plain; }
	unsigned long long t1 = now_ns();
	logf("speed plain global: %llu ns/access", (t1 - t0) / SPEED_ITERS);

	t0 = now_ns();
	for (unsigned long long i = 0; i < SPEED_ITERS; i++) { volatile int* q = (volatile int*)__emutls_get_address(&g_e_ctl); p += *q; }
	t1 = now_ns();
	logf("speed E (stock __emutls_get_address): %llu ns/access", (t1 - t0) / SPEED_ITERS);

	t0 = now_ns();
	for (unsigned long long i = 0; i < SPEED_ITERS; i++) { volatile int* q = (volatile int*)ps5_fast_emutls_get_address(&g_f_ctl); p += *q; }
	t1 = now_ns();
	logf("speed F (ps5_fast_emutls_get_address): %llu ns/access", (t1 - t0) / SPEED_ITERS);
	g_sink += (unsigned long long)p;
}
#endif

#ifdef PROBE_N
static void speed_n()
{
	int p = 0;
	const unsigned long long t0 = now_ns();
	for (unsigned long long i = 0; i < SPEED_ITERS; i++) { g_a = (int)i; p += g_a; }
	const unsigned long long t1 = now_ns();
	logf("speed N (native thread_local): %llu ns/access", (t1 - t0) / SPEED_ITERS);
	g_sink += (unsigned long long)p;
}
#endif

#ifdef PROBE_N
static void run_n_layout()
{
	uintptr_t fs0;
	__asm__ volatile("movq %%fs:0, %0" : "=r"(fs0));
	logf("N: fs0=0x%" PRIxPTR " &magic=0x%" PRIxPTR " (compiler offset %lld)", fs0, (uintptr_t)&g_magic,
		(long long)((intptr_t)&g_magic - (intptr_t)fs0));
	// scan [fs0 - 128 KiB, fs0) for the magic value
	const uintptr_t lo = fs0 > 0x20000 ? fs0 - 0x20000 : 0;
	int found = 0;
	for (uintptr_t p = lo; p + sizeof(uint64_t) <= fs0; p += 8)
	{
		uint64_t v;
		memcpy(&v, (const void*)p, sizeof v);
		if (v == kMagic)
		{
			logf("N: magic found at fs0%+lld (0x%" PRIxPTR ")", (long long)((intptr_t)p - (intptr_t)fs0), p);
			found++;
			if (found >= 4) break;
		}
	}
	if (!found)
		logf("N: magic NOT found in [fs0-128KiB, fs0) (image not copied, or block elsewhere)");
	logf("N: value at &magic = 0x%llx", g_magic);
}
#endif

int main(int, char**)
{
	char path[64];
	snprintf(path, sizeof path, "/app0/tlsprobe-%s.txt", PROBE_TAG);
	g_log = fopen(path, "w");
	logf("=== TLS probe %s ===", PROBE_TAG);

	run_common();
	run_destructors();
#ifdef PROBE_EF
	run_f_test();
	speed_ef();
#endif
#ifdef PROBE_N
	run_n_layout();
	speed_n();
#endif

	logf("=== done ===");
	if (g_log) fclose(g_log);
	return 0;
}

extern "C" void catchReturnFromMain(int status)
{
	(void)status;
	if (g_log) fclose(g_log);
	sceSystemServiceLoadExec("exit", nullptr);
	for (;;)
		sceKernelUsleep(100000);
}
