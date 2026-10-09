# KB-RPCS3 Alpha 0.1.0

[![version alpha 0.1.0](https://img.shields.io/badge/version-alpha%200.1.0-d9822b?style=flat-square)](https://github.com/kamb205/KB-RPCS3/releases/tag/v0.1.0-alpha)
[![source release](https://img.shields.io/badge/status-source%20release-1f6feb?style=flat-square)](https://github.com/kamb205/KB-RPCS3)
[![platform PS5 homebrew](https://img.shields.io/badge/platform-PS5%20homebrew-0aa2c0?style=flat-square)](https://github.com/kamb205/KB-RPCS3)

KB-RPCS3 runs [RPCS3](https://rpcs3.net), the open-source PlayStation 3 emulator, as a
native PS5 homebrew title. This is its **first public release**: the complete source of
the port, the RPCS3 patch series, the PS5 title shell and the build instructions.

**This is a source release. There is no downloadable PS5 package yet** — a binary that
combines RPCS3's GPL-2.0-only core with GPL-3.0 PS5 components cannot be distributed,
and the current combined executable has not passed final PS5 testing. See
[Licensing](#licensing) and `LICENSING_STATUS.md` in the source.

> **Unofficial.** Not made, endorsed or supported by the RPCS3 project or by Sony
> Interactive Entertainment. "PlayStation", "PS3" and "PS5" are trade marks of Sony
> Interactive Entertainment Inc.

## Downloads

| Asset | What it is |
|---|---|
| `KB-RPCS3-0.1.0-Source.tar.gz` | The repository at tag `v0.1.0-alpha` — RPCS3 patch series, PS5 title shell, tools, licences and documentation |
| `SHA256SUMS` | SHA-256 checksums for the assets above |
| PS5 package | **Not available.** No binary is published, tested or promised |

## Key features

- **RPCS3 on the console** — the emulator core built for the PS5, rendering through
  Mesa/RADV Vulkan directly on the hardware.
- **Native home screen** (title `PPSA99303`, shown as **KB-RPCS3**) with **Games**,
  **Install**, **Settings** and **System** tabs.
- **Global and per-game settings**, with recommended settings from RPCS3's public
  configuration database when available.
- **Installers** for PS3 firmware (`PS3UPDAT.PUP`), PSN packages (`.pkg`) and licences
  (`.rap`).
- **PS5 home-screen tiles** that launch a game directly.
- **DualSense** controls, vibration, save data and trophy notifications.
- **Benchmark tooling** that records the effective game speed, so results can be shown
  to have been taken at normal speed.

## What works

Everything in the feature list above was working in the development build this release
is based on. The Alpha 0.1.0 source adds speed/timing logging and off-by-default
experimental options on top of that build; that exact source and the new branding have
**not** yet been run on a console.

## Tested games

Verified on **one PS5 Slim** (firmware 13.60, etaHEN, kstuff lite, ShadowMountPlus) at
**normal game speed** (Clocks scale 100, Frame limit Auto):

| Game | Serial | Result |
|---|---|---|
| **LIMBO** | `HPEB00564` | Playable — picture, sound and controls. |
| **WWE '12** | `BLES01439` | Playable — menus near 60 FPS; matches slow down during big moves (CPU-limited). |
| **Grand Theft Auto IV** | `BLES01128` | Boots and plays, with substantial performance limitations — roughly 13–27 FPS when driving, occasional audio stutter. Needs "SPU XFloat Accuracy: Accurate" or cars fall through the road. |

Every other PS3 game is **untested**. GTA IV results recorded with **Clocks scale 300**
(around 20–22 FPS) count as benchmarks only — in that mode the game runs about three
times too fast and they are **not** normal-speed performance.

## Important limitations

- **No PS5 binary.** Licence conflict; see below.
- **Alpha 0.1.0 has not finished console regression testing.**
- **No performance improvement is claimed** — the experimental SPU options are off by
  default and unmeasured at normal speed.
- **CPU-limited.** Demanding games are bound by emulated SPU performance, not the GPU.
- **First-visit stutter** while SPU code and shaders compile; both are cached afterwards.
- **Native TLS does not work on the PS5 yet** — emulated TLS is used.

## Installation and build

There is nothing to install from this release on the console. Build it yourself, for
your own use:

```bash
port/apply-rpcs3-patches.sh /path/to/rpcs3
SRC_DIR=/path/to/rpcs3 BUILD_DIR=/path/to/rpcs3-build JOBS=8 port/rpcs3-build.sh rpcs3_ps5
APP_DIR=/path/to/app-build-copy RPCS3_BUILD_DIR=/path/to/rpcs3-build tools/vm-build-app.sh
```

Then deploy `dist/PPSA99303/` to the console as described in `INSTALL.md`. Do not
redistribute a build you make: a combined binary cannot be distributed today. Full
details, dependencies and the toolchain layout are in `BUILDING.md`.

## Requirements

- A **jailbroken PS5** (developed on a PS5 Slim, firmware 13.60) with a homebrew
  enabler (etaHEN), **kstuff lite** and [ShadowMountPlus](https://github.com/drakmor/ShadowMountPlus).
- Your own legally obtained **PS3 firmware** and **games**.
- A **Linux/arm64 build host** with the PS5 toolchain for building from source.

KB-RPCS3 includes none of the firmware, games, keys or tools above.

## Known issues

See `KNOWN_ISSUES.md` in the source for the full list, including:

- The build is tied to the development machine's fixed toolchain paths.
- Native TLS is broken on the PS5; do not build with `-fno-emulated-tls`.
- The console's own screenshot feature captures black while KB-RPCS3 is open; L3 + R3 on
  the home screen saves a screenshot to `rpcs3/screenshots/` instead.
- This is an alpha: expect crashes. Keep a known-good build to fall back to
  (`ROLLBACK.md`).

## Credits

KB-RPCS3 is a port and would not exist without **[RPCS3](https://github.com/RPCS3/rpcs3)**
by the RPCS3 team and contributors, **Mihawk** (PS5_Vulkan, the PS5 Vulkan Template and
the PS5 platform layer), **BlackBearReloaded** (`ps5-homebrew-ui`), **ps5-payload-dev**
(the PS5 payload SDK), **Sascha Willems** (Vulkan example base) and the **Mesa/RADV
developers**. Full authorship and licence details are in `THIRD_PARTY_NOTICES.md`.

## Licensing

This release is made of **separately licensed parts**: the RPCS3 patch series
(`port/rpcs3-patches/`) under RPCS3's **GPL-2.0-only**; the PS5 title shell (`app/`)
under **MIT** and **GPL-3.0-or-later** per file. GPL-2.0-only and GPL-3.0 code cannot
be combined in one distributed program, which is why this release is source only.
See `LICENSING_STATUS.md`, `app/LICENSING.md`, `LICENSES/` and `COPYING`.

No PS3 or PS5 firmware, games, keys, system files or other Sony content is included.
Use your own legally obtained firmware and games.
