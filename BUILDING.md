# Building KB-RPCS3 from source

KB-RPCS3 has two separately licensed parts that are combined only at build time, on your own machine:

| Part | Folder | Licence |
|---|---|---|
| **RPCS3 + the KB-RPCS3 core patches** | `port/rpcs3-patches/` (applied to upstream RPCS3) | **GPL-2.0-only** (RPCS3's licence) |
| **The PS5 title shell** (home screen, settings, installer, PS5 platform glue) | `app/` | mixed — see `app/LICENSING.md` (MIT base, GPL-3.0-or-later UI and title code) |

Read `LICENSING_STATUS.md` before distributing anything you build: a binary that combines both parts
cannot currently be distributed.

> **Status of these instructions:** they describe the build exactly as it was done for Alpha 0.1.0. That
> build ran in an Ubuntu (arm64) virtual machine with the PS5 toolchain installed at fixed paths under
> `~/work/`. The scripts in `port/` and `tools/` still assume that layout. Reproducing the build on
> another machine requires the same dependencies at the same paths, or editing the paths in the
> scripts. A portable build is planned; it is not part of this alpha.

## 1. Dependencies (fetched separately, not included here)

| Dependency | Used for | Licence |
|---|---|---|
| Upstream **RPCS3** at revision `46aee28f8` (with its git submodules) | the emulator core | GPL-2.0-only (+ submodule licences) |
| **PS5_Vulkan** (Mihawk) — RADV/Mesa build for the PS5, link recipe, native ELF/SELF tool, `libc.prx` | graphics driver and title packaging | GPL-3.0 (Mesa/RADV parts: MIT) |
| **ps5-payload-sdk** (ps5-payload-dev) and its fork with the PS5 platform layer (Mihawk) | toolchain, headers, platform library | GPL-3.0-or-later |
| **PS5_VKHomebrewUI** (Mihawk's fork of BlackBearReloaded's ps5-homebrew-ui) | the UI kit, fetched by `app/ps5/ui/setup-kit.sh` | GPL-3.0 |
| LLVM, FFmpeg, libiconv and zlib built for the PS5 | linked by the emulator core | Apache-2.0 WITH LLVM-exception, LGPL/GPL, LGPL, Zlib |
| Vulkan-Headers 1.4.354 and volk 1.4.350.1 | Vulkan loading | Apache-2.0 / MIT |

The LLVM/FFmpeg/libiconv/zlib PS5 builds used for the alpha came from a separate PS5 RetroArch build's
dependency folder; `port/rpcs3-build.sh` shows the exact paths it expects.

## 2. Apply the RPCS3 patch series

```bash
port/apply-rpcs3-patches.sh /path/to/rpcs3
```

This clones upstream RPCS3, checks out `46aee28f8`, initialises the submodules and applies the 47
patches with `git am --keep-cr`. The `--keep-cr` is required: some upstream files use CRLF line endings.

## 3. Build the emulator core

```bash
SRC_DIR=/path/to/rpcs3 BUILD_DIR=/path/to/rpcs3-build JOBS=8 port/rpcs3-build.sh rpcs3_ps5
```

- Leave `EXTRA_CFLAGS` empty. The working configuration uses **emulated TLS**. Native TLS
  (`-fno-emulated-tls`) failed on real hardware (`KNOWN_ISSUES.md`).
- Set `CCACHE_DISABLE=1` when you need trustworthy compiler diagnostics.
- A full build takes about two hours on an 8-core machine. It ends with `BUILD OK` and writes
  `rpcs3-link-inputs.txt`, the list of static archives the title links.

## 4. Build the title

```bash
APP_DIR=/path/to/app-build-copy RPCS3_BUILD_DIR=/path/to/rpcs3-build tools/vm-build-app.sh
```

The title is written to `dist/PPSA99303/`. Do not set `PS5_NATIVE_TLS=1`.

Optional: the per-game recommended settings need RPCS3's configuration database, which is not
redistributed here. Download it before building:

```bash
mkdir -p app/examples/rpcs3ps5/data
curl -o app/examples/rpcs3ps5/data/rpcs3-config-db.json 'https://api.rpcs3.net/config/?api=v1'
```

## 5. Experimental options

All experimental SPU options are **off by default**. They are switched on per console with a tuning
file, `rpcs3/ps5-tuning.txt` in the title folder, one `key value` per line:

| Key | Default | Meaning |
|---|---|---|
| `spu_insert_select` | 0 | SPU LLVM: variable-index insert as a lane select |
| `spu_narrow_ls_store` | 0 | SPU LLVM: narrow local-store store for load → insert → store |
| `spu_narrow_ls_check` | 0 | debug: verify every narrow store against the full store |
| `spu_hot_cpu` | -1 | keep the busiest SPU thread on the given CPU (12 = the CPU without an SMT sibling) |
| `spu_jit_verify` | 0 | debug: LLVM IR verification of all compiled SPU code |

None of these is a validated improvement yet.
