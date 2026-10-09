# KB-RPCS3 Alpha 0.1.0 — release notes (2026-10-09)

The first public release of KB-RPCS3, an unofficial port of the RPCS3 PlayStation 3 emulator to
jailbroken PS5 consoles.

## Downloads

| File | What it is |
|---|---|
| `KB-RPCS3-0.1.0-Source.tar.gz` | The complete source release: RPCS3 patch series, PS5 title shell, tools, documentation, licences |
| PS5 package | **Not available yet.** Distribution of a PS5 binary is pending a licence resolution; see `LICENSING_STATUS.md`. |

## What KB-RPCS3 can do

- Run RPCS3's emulator core as a native PS5 homebrew title (`PPSA99303`, shown as **KB-RPCS3**).
- Home screen with Games, Install, Settings and System tabs; global and per-game settings; recommended
  settings for games in RPCS3's database; PS5 home-screen tiles for games.
- Install PS3 firmware, PKG games and RAP licences on the console.
- Tested on one PS5 (firmware 13.60): LIMBO and WWE 12 playable; GTA IV playable at roughly
  13–27 FPS when driving, at normal game speed.

## New in this release

- KB-RPCS3 name, version and an original icon.
- The emulator logs the effective game speed at boot (`PS5 SPEED`) and the measured emulated-time ratio
  and vblank rate while a game runs (`PS5 TIMING`), so recorded results show they were taken at normal
  speed.
- Benchmark scripts use normal speed by default; the uncapped 300 % mode needs an explicit opt-in.
- Experimental SPU options, all **off by default** and not yet measured: LLVM insert-select, hot-SPU
  CPU pinning, narrow local-store stores, plus LLVM IR verification and a narrow-store equivalence check.
- Build fixes for LLVM 22 and for clang 18 with `-Werror=return-type`.

## Not in this release

- No PS5 binary (licensing).
- No performance improvement is claimed: the experimental options are untested at normal speed.
- Native TLS: it does not work on the PS5 yet; the release uses emulated TLS.

## Testing status

The games above were tested with the development build this release is based on. The Alpha 0.1.0
source adds logging and off-by-default options on top of it; that exact source and the new branding
have **not** yet been run on a console.

## Credits

RPCS3 by the RPCS3 team and contributors; PS5 homebrew stack by Mihawk, BlackBearReloaded,
ps5-payload-dev and the Mesa/RADV developers. See `THIRD_PARTY_NOTICES.md`.
