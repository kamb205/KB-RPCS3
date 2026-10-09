# TLS probes — offline build results (2026-10-09)

No console access was used. Both titles were built in the VM with the same SDK, compiler wrappers and
link recipe as the RPCS3 title (`tools/launcher/build.sh` is the template). They were **not** deployed.

Build: `limactl shell ps5build -- bash tools/tlsprobe/build.sh EF|N` (from the Mac combo worktree).

## PPSA99391 (EF) — emulated TLS
- Output: `~/work/tlsprobe-EF/app/eboot.bin` (556,179 bytes).
- `obj/probe.elf` (linker output): **no PT_TLS**.
- `obj/eboot.elf` (after `ps5-native-tool link`): zero-size TLS header (filesz 0, memsz 0, align 1) —
  the same shape as the known-good RPCS3 title.
- Entry points: `__emutls_get_address` (t, from libc.a) and `ps5_fast_emutls_get_address` (T, our F).
- The title also emits `__emutls_v.g_a`, `__emutls_v.g_magic`, `__emutls_v.g_z`, `__emutls_v.g_tracked`
  (the E/F controls) plus a `movq %fs:0` in `f_tcb()`.

## PPSA99392 (N) — native TLS
- Output: `~/work/tlsprobe-N/app/eboot.bin` (554,419 bytes).
- `obj/probe.elf`: **PT_TLS** vaddr 0x8c718, filesz **0x10**, memsz **0x31**, align 0x8.
- `obj/eboot.elf`: PT_TLS filesz **0x10**, memsz **0x31**, align 0x20 (the converter rewrites align).
- `probe.o` relocations: **`R_X86_64_GOTTPOFF`** (initial-exec), matching the handoff's native-TLS note.
- TLS block layout lld assigned (st_value in `obj/probe.elf`):

  | symbol | offset | size | section |
  |---|---|---|---|
  | `g_a` | 0x00 | 4 | .tdata |
  | `g_magic` | 0x08 | 8 | .tdata |
  | `g_z` | 0x10 | 8 | .tbss |
  | `g_tracked` | 0x18 | 24 | .tbss |
  | `__tls_guard` | 0x30 | 1 | .tbss |

- The runtime `%fs` offset per variable is what the on-console layout diagnosis in `probe.cpp` prints
  (`fs0`, `&magic - fs0`, and a 128 KiB scan below `fs0` for the magic value). Offline we can only
  record the TLS-block offsets above; the `GOTTPOFF` GOT slot is zero in the file and is filled by the
  loader, so the signed TPOFF is not visible statically.

## Test coverage in `probe.cpp` (runs on the console; not run here)
- initial values of `a=42`, `magic=0x5EC0DE00A11CE5ED`, `z=0`; each thread's own copy;
  main thread unchanged; distinct addresses across 16 threads (8 `std::thread` + 8 `pthread_create`).
- 1,000,000 self-increments per thread (isolation).
- 2,000 sequential create/join (fresh initial values each time).
- destructor counts for `std::thread` return, raw return, `pthread_exit`, `ps5_pthread_exit`.
- `SIGUSR1` handler read/write of a `thread_local`, on the main and a second thread.
- E/F: `__emutls_get_address` and `ps5_fast_emutls_get_address` must return the same address for the
  same `__emutls_v.*` control; 1e8-access ns/access for plain / E / F.
- N: `%fs:0`, `&magic - fs0`, the magic scan, and 1e8-access ns/access for native.
- Results are appended to `/app0/tlsprobe-<variant>.txt`; the title ends with
  `sceSystemServiceLoadExec("exit")`.

## Revision 2 — review fixes (2026-10-09)

Fixed in `fast_emutls.c` / `probe.cpp` / `build.sh`:
- **Cache lifetime / use-after-free**: removal now clears the slot value **before** tombstoning the key, so
  a reader that already observed `key == tcb` loads `NULL`, never a pointer about to be freed. Insertion
  into an empty/tombstoned slot publishes a `NULL` value before claiming the slot, and an existing entry
  for the same tcb is updated in place (verified by the live-entry count below).
- **Allocation failures**: `malloc`/`realloc`/`pthread_key_create`/`pthread_setspecific` failures now
  `abort()` instead of dereferencing a NULL array (a failed `realloc` no longer loses the old array).
- **Invalid test design removed**: E and F no longer share a `__emutls_control`; the E/F
  address-equality assertion is gone. F is tested on controls of its own (`g_f_ctl`), E on `g_e_ctl`.
- **EF2 production-style** (`PPSA99393`): `-DTLS_F_AS_STOCK` gives a strong `__emutls_get_address` that
  forwards to F, so the whole title uses F (as a release would) and the common tests validate it.
- **Cache-lifetime check**: `ps5_fast_emutls_cache_live()` counts live entries; after 8 worker threads
  exit it must be 1 (the main thread's).
- **Logging**: results are written to `/app0/tlsprobe-<variant>.txt` and mirrored to the kernel log
  (`sceKernelDebugOutText`), so a failed file write still leaves evidence.

Build results (all three built OK, no PT_TLS in `probe.elf`; only the converter's zero-size TLS header in
`eboot.elf`):

| title | variant | eboot.bin | `__emutls_get_address` |
|---|---|---|---|
| PPSA99391 | EF (E stock + F) | 557,507 B | `t` (libc.a weak) |
| PPSA99393 | EF2 (F as the provider) | 556,291 B | `T` (our strong F) |
| PPSA99392 | N (native TLS) | 554,419 B | — (PT_TLS 0x10/0x31) |

Still **not deployed**; the runtime tests need the console.


- Heap growth across the 2,000 threads (`ps5_heap_stats`) is declared in the probe but not yet wired into
  a leak check.
- `scePthreadCreate` (the SDK does not expose it) — raw `pthread_create` only.
- The runtime results themselves (the PS5 is off).
