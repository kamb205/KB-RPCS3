# KB-RPCS3 — Licensing status (Alpha 0.1.0, 2026-10-09)

This document records what was checked in the actual sources, why no PS5 binary is published, and what
would unblock one. It is a factual record, **not legal advice**.

## 1. Summary

- **No PS5 binary (eboot.bin, folder title or package image) is published.** The title statically links
  the RPCS3 core, which is **GPL-2.0-only**, with PS5 components that are **GPL-3.0 / GPL-3.0-or-later**.
  GPL-2.0-only and GPL-3.0 code cannot be combined in one distributed program.
- **Alpha 0.1.0 is a source release made of two separately licensed parts**, each distributed under its
  own licence, plus documentation:
  1. `port/rpcs3-patches/`: changes to RPCS3, **GPL-2.0-only**, like RPCS3 itself;
  2. `app/` and `tools/`: the PS5 title shell and helper tools, under the licences in their file
     headers (MIT and GPL-3.0-or-later; see `app/LICENSING.md`). KB's scripts in `tools/` and `port/`
     that have no header are GPL-3.0-or-later, except `port/apply-rpcs3-patches.sh` (GPL-2.0-only).
- RPCS3's licence is **not** changed, and no copyright or licence notice has been removed.

## 2. What the title links (checked 2026-10-09)

| Component | Copyright holder(s) | Licence (as found) |
|---|---|---|
| RPCS3 emulator core (static archives) | RPCS3 team and contributors | GPL-2.0-only (`LICENSE`; README: "Most files are licensed under … GPL-2.0-only") |
| KB-RPCS3 core patches: `rpcs3/ps5/*`, changes to RPCS3 files | KB (patches to RPCS3) | GPL-2.0-only (SPDX headers; files without a header follow RPCS3's default) |
| Title program and kit (`app/examples/rpcs3ps5/kit/*`), PS5 helpers (`app/ps5/src/ps5_env.c`, `ps5_dl.c`, `ps5_fastclock.c`, `ps5_tuning.c`, `import_check.c`, `probe_rpcs3.cpp`) | KB | GPL-3.0-or-later |
| `app/examples/rpcs3ps5/kit/ps5_frontend.h` (the C interface between the title and the core) | KB | GPL-2.0-only (the same file as `rpcs3/ps5/ps5_frontend.h`) |
| PS5 Vulkan Template: title program base (`rpcs3ps5.cpp`) and UI module (`app/ps5/ui/*`) | Mihawk | GPL-3.0-or-later |
| PS5 Vulkan Template: Vulkan example base, PS5 platform glue (`app/base/*`, `app/ps5/src/platform.c` …) | Sascha Willems; Mihawk | MIT |
| PS5_Vulkan: RADV/Mesa build, `app_crt.cpp`, `libc.prx` | Mesa contributors; Mihawk | Mesa/RADV: MIT; PS5_Vulkan code: GPL-3.0 / GPL-3.0-or-later |
| ps5-payload-sdk and its PS5 platform layer (`libps5platform.a`) | ps5-payload-dev; Mihawk (fork and platform layer) | GPL-3.0-or-later |
| ps5-homebrew-ui kit (via PS5_VKHomebrewUI) and its sounds and music | BlackBearReloaded; Mihawk (fork) | GPL-3.0 (assets: GPL-3.0-or-later) |
| LLVM runtimes (libc++, libc++abi, libunwind, builtins) | LLVM contributors | Apache-2.0 WITH LLVM-exception |
| FFmpeg (static archives) | FFmpeg developers | LGPL-2.1-or-later, or GPL-2.0-or-later if built with `--enable-gpl` — **not yet confirmed** |
| zlib, Vulkan-Headers, volk, GLM, ImGui, KTX, tinygltf | their authors | Zlib, Apache-2.0, MIT |

## 3. Why the binary is blocked

The GPL-3.0(-or-later) parts above are linked into the same `eboot.bin` as the GPL-2.0-only RPCS3 core.
GPL-2.0-only code may only be distributed as part of a whole that is itself licensed under the GPL-2.0;
GPL-3.0 code may only be distributed as part of a whole under the GPL-3.0. No single licence satisfies
both, so the combined binary cannot be distributed.

## 4. Fastest lawful route to a binary

Asking RPCS3 to relicense is not realistic: RPCS3 has hundreds of copyright holders. The GPL-3.0 side
has **few** copyright holders, so the realistic route is to make every linked non-RPCS3 component
available under terms compatible with GPL-2.0-only:

1. **KB's own GPL-3.0-or-later code** (the kit, the PS5 helpers): KB can additionally license it under
   GPL-2.0-or-later. This is the maintainer's own decision; it has not been made yet.
2. **Mihawk**: the PS5 Vulkan Template UI module and title program base, PS5_Vulkan's linked code
   (`app_crt.cpp`, the `libc.prx` module), and the PS5 platform layer. Needs written permission to use
   them under GPL-2.0-compatible terms (for example dual licensing as GPL-2.0-or-later, or MIT).
3. **BlackBearReloaded**: the ps5-homebrew-ui kit code (the sounds and music could be replaced instead).
   Needs the same permission.
4. **ps5-payload-dev**: confirm which SDK objects are linked into a title (startup code, stub libraries,
   header code) and under what terms.
5. Confirm FFmpeg's build licence. LGPL-2.1-or-later and GPL-2.0-or-later are both compatible with RPCS3;
   a `--enable-version3` build would not be.

Draft requests are in `PERMISSION_REQUESTS.md`. None has been sent yet.

The alternative, replacing the GPL-3.0 UI kit, UI module and platform layer with GPL-2.0-compatible
code, would take substantial development time. It is not planned for this alpha.

## 5. Why the source release is distributed

- Each part is distributed under its own licence, with its licence texts (`LICENSES/`) and with all
  original notices kept. RPCS3's changes are distributed as patches to RPCS3, under RPCS3's licence.
- Both GPL versions allow different programs to be distributed together on the same medium ("mere
  aggregation"). They do not allow an incompatible combined work to be distributed, and this release
  does not distribute one: no binary, and no pre-combined source tree.
- Building and running the combination for your own use is not distribution. Distributing a binary you
  built would be, and is subject to section 3.
- **Residual risk:** whether a separately distributed title shell, written to be linked with RPCS3, is
  itself a derivative of RPCS3 is not settled law. One file makes the boundary less clean today:
  `app/examples/rpcs3ps5/kit/ps5_frontend.h` is GPL-2.0-only inside the GPL-3.0-or-later title shell.
  The maintainer can resolve this by additionally licensing that interface header (their own file)
  under MIT.

## 6. Package format note

The PS5 Vulkan toolchain produces folder titles and mountable images (`.ffpkg` via UFS2Tool,
`.ffpfsc` via MkPFS), which ShadowMountPlus registers on the home screen. It does not produce a
Sony-style installer `.pkg`. When a binary is cleared, the planned downloads are a `.ffpfsc` image and a
ZIP of the title folder.
