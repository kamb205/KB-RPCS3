/*
 * RPCS3 PS5 - the feasibility probe: what RPCS3 needs from the console, measured in
 * this title's own process.
 *
 * A launch that finds /app0/platform-probe.txt runs it before the screen starts and
 * deletes the file. Its words select the optional tests of the platform layer's probe
 * (ps5platform/probe.h): "large", "huge", "full", "jit", "threads", "topology"; "all"
 * selects every one of them but "jit" (a refused import may end the process). Every
 * line goes to klog and to /app0/probe-results.txt, flushed at once, so a probe that
 * crashes keeps what came before it.
 *
 * Besides the platform probe it checks what is RPCS3's own:
 *   - the guest address layout RPCS3 reserves (utils/memory.cpp, Emu/Memory/vm.cpp):
 *     8, 12, 32 and 4 GiB ranges one after another from 0x10_0000_0000;
 *   - executable memory as LLVM's JIT will use it: read-write-execute, written,
 *     run, rewritten and run again;
 *   - the CPUs the title gets and the console's pools and system software.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <thread>

#if !defined(PS5_HOST_REFERENCE)
#include <ps5platform/exec.h>
#include <ps5platform/kernel.h>
#include <ps5platform/probe.h>
#include <ps5platform/shm.h>
#endif

#include "platform.h"

namespace {

const char *const probePath = "/app0/platform-probe.txt";
const char *const resultsPath = "/app0/probe-results.txt";
FILE *results = nullptr;

__attribute__((format(printf, 1, 2))) void line(const char *format, ...)
{
	char text[512];
	va_list args;
	va_start(args, format);
	vsnprintf(text, sizeof(text), format, args);
	va_end(args);
	say("probe: %s", text);
	if (results) {
		fprintf(results, "%s\n", text);
		fflush(results);
	}
}

#if !defined(PS5_HOST_REFERENCE)
void probeLine(void *, const char *text)
{
	line("%s", text);
}

double now()
{
	timespec t{};
	clock_gettime(CLOCK_MONOTONIC, &t);
	return t.tv_sec + t.tv_nsec / 1e9;
}

int checkSystem()
{
	ps5_kernel_sw_version version{};
	version.size = sizeof(version);
	if (sceKernelGetSystemSwVersion(&version) == 0)
		line("system: software %.*s (0x%08x)", (int)sizeof(version.text), version.text, version.version);
	size_t flexible = 0;
	sceKernelAvailableFlexibleMemorySize(&flexible);
	line("system: direct memory %lld MiB, flexible free %zu MiB, hardware_concurrency %u, tsc %llu Hz",
		(long long)(sceKernelGetDirectMemorySize() >> 20), flexible >> 20,
		std::thread::hardware_concurrency(), (unsigned long long)sceKernelGetTscFrequency());
	return 0;
}

/* RPCS3's guest layout: its 8, 12, 32 and 4 GiB ranges, one after another from
 * 0x10_0000_0000 (as the platform's notes say the RetroArch title reserved them). */
int checkGuestLayout()
{
	static const struct { const char *what; uint64_t gib; } ranges[] = {
		{ "vm::g_base_addr (guest 4 GiB + guard)", 8 },
		{ "vm::g_sudo_addr + exec", 12 },
		{ "SPU/RSX mirrors", 32 },
		{ "vm::g_stat_addr", 4 },
	};
	int failures = 0;
	void *bases[4] = {};
	uint64_t sizes[4] = {};
	uintptr_t hint = 0x1000000000ull;
	for (int i = 0; i < 4; ++i) {
		const uint64_t bytes = ranges[i].gib << 30;
		void *base = nullptr;
		const int error = ps5_vrange_reserve(bytes, reinterpret_cast<void *>(hint), 1ull << 30, &base);
		if (error) {
			line("guest layout: %s, %llu GiB: REFUSED (%d)", ranges[i].what, (unsigned long long)ranges[i].gib, error);
			++failures;
			continue;
		}
		line("guest layout: %s, %llu GiB at %p", ranges[i].what, (unsigned long long)ranges[i].gib, base);
		bases[i] = base;
		sizes[i] = bytes;
		hint = reinterpret_cast<uintptr_t>(base) + bytes;
	}
	// A page of guest memory committed and written inside the first range, as
	// vm::falloc does for the first allocations
	if (bases[0]) {
		void *page = static_cast<char *>(bases[0]) + 0x10000;
		if (ps5_vrange_commit(page, 0x10000, 3 /* read | write */) == 0) {
			std::memset(page, 0xa5, 0x10000);
			const bool right = static_cast<unsigned char *>(page)[0xffff] == 0xa5;
			line("guest layout: 64 KiB committed and written: %s", right ? "ok" : "WRONG");
			failures += right ? 0 : 1;
			ps5_vrange_decommit(page, 0x10000);
		} else {
			line("guest layout: commit REFUSED");
			++failures;
		}
	}
	for (int i = 3; i >= 0; --i)
		if (bases[i])
			ps5_vrange_release(bases[i], sizes[i]);
	return failures;
}

/* Executable memory as the JIT uses it: 64 MiB near the title's code, a function
 * written, run, rewritten in place and run again with no protection change. */
int checkJitMemory()
{
	const size_t bytes = 64u << 20;
	const double start = now();
	auto *code = static_cast<unsigned char *>(
		ps5_exec_allocate(bytes, reinterpret_cast<uintptr_t>(&checkJitMemory)));
	if (!code) {
		line("jit: 64 MiB of executable memory: REFUSED");
		return 1;
	}
	// mov eax, imm32; ret
	auto emit = [&](uint32_t value) {
		code[0] = 0xb8;
		std::memcpy(code + 1, &value, 4);
		code[5] = 0xc3;
		__builtin___clear_cache(reinterpret_cast<char *>(code), reinterpret_cast<char *>(code + 6));
	};
	using Function = uint32_t (*)();
	int failures = 0;
	for (uint32_t value : { 0x50533352u, 0x12345678u, 0xcafef00du }) {
		emit(value);
		const uint32_t got = reinterpret_cast<Function>(code)();
		if (got != value)
			++failures;
	}
	// A function at the far end of the region, as a full code cache would place it
	unsigned char *far = code + bytes - 4096;
	far[0] = 0xb8;
	const uint32_t marker = 0x0f0f0f0fu;
	std::memcpy(far + 1, &marker, 4);
	far[5] = 0xc3;
	failures += reinterpret_cast<Function>(far)() == marker ? 0 : 1;
	const long distance = static_cast<long>(reinterpret_cast<intptr_t>(code) - reinterpret_cast<intptr_t>(&checkJitMemory));
	line("jit: 64 MiB executable at %p (%+ld MiB from the title's code), write/run/rewrite: %s, %.2f ms",
		code, distance >> 20, failures ? "WRONG" : "ok", (now() - start) * 1e3);
	ps5_exec_release(code);
	return failures;
}

/* Threads as RPCS3 starts them: one per PPU/SPU thread and more, each spinning. */
int checkThreads()
{
	const double start = now();
	std::thread workers[16];
	unsigned long long sums[16] = {};
	for (int i = 0; i < 16; ++i)
		workers[i] = std::thread([&sums, i] {
			unsigned long long sum = 0;
			for (unsigned long long n = 0; n < 50000000ull; ++n)
				sum += n ^ i;
			sums[i] = sum;
		});
	for (auto &worker : workers)
		worker.join();
	line("threads: 16 threads x 50M iterations in %.2f s (%s)", now() - start, sums[15] ? "ok" : "WRONG");
	return sums[15] ? 0 : 1;
}
#endif

} // namespace

/* Runs the probe when it was asked for; true when it ran. */
bool rpcs3_probe_if_requested()
{
	FILE *request = fopen(probePath, "r");
	if (!request)
		return false;
	char words[128] = {};
	const size_t read = fread(words, 1, sizeof(words) - 1, request);
	words[read] = '\0';
	fclose(request);
	remove(probePath);
	results = fopen(resultsPath, "w");
	line("begins: words \"%s\"", words);
	int failures = 0;
#if !defined(PS5_HOST_REFERENCE)
	const bool all = strstr(words, "all") != nullptr;
	failures += checkSystem();
	failures += checkGuestLayout();
	failures += checkJitMemory();
	failures += checkThreads();
	unsigned flags = 0;
	if (all || strstr(words, "large"))
		flags |= PS5_PROBE_LARGE;
	if (all || strstr(words, "huge"))
		flags |= PS5_PROBE_HUGE;
	if (all || strstr(words, "full"))
		flags |= PS5_PROBE_FULL;
	if (strstr(words, "jit"))
		flags |= PS5_PROBE_JIT_API;
	failures += ps5_platform_probe(probeLine, nullptr, flags);
	if (all || strstr(words, "threads"))
		failures += ps5_platform_probe_threads(probeLine, nullptr);
	if (all || strstr(words, "topology"))
		failures += ps5_platform_probe_topology(probeLine, nullptr);
#endif
	line("ends: failures %d", failures);
	if (results) {
		fclose(results);
		results = nullptr;
	}
	return true;
}
