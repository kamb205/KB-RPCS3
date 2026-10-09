# Known issues — Alpha 0.1.0

## No PS5 binary

The PS5 title can't be distributed yet because of a licence conflict (`LICENSING_STATUS.md`). Build it
yourself for your own use (`BUILDING.md`).

## The build is tied to the development machine

`port/rpcs3-build.sh` and `tools/vm-build-app.sh` expect the PS5 toolchain and dependencies at fixed
paths inside a Lima virtual machine named `ps5build`. Building elsewhere requires adapting the paths.

## Performance

- Demanding games are limited by the PS5's CPU running PS3 SPU code. GTA IV runs at roughly 13–27 FPS
  when driving.
- The first time a game area is visited, SPU code and shaders are compiled, which can cause stutter.
  Both are cached and compiled before play on later visits.

## Experimental options (off by default)

Three SPU optimisations — LLVM insert-select, hot-SPU CPU pinning, narrow local-store stores — plus two
debug checks can be switched on with a tuning file (`BUILDING.md` §5). None of them has been measured at
normal game speed yet. The narrow-store option must not be used before its equivalence check
(`spu_narrow_ls_check 1`) has run clean on real hardware.

## Native TLS does not work on the PS5

KB-RPCS3 uses compiler-emulated thread-local storage. A test build with native TLS started, but every
`thread_local` with a non-zero initial value read as zero, which broke RPCS3 (GTA IV ran at about
0.5 FPS with fatal "Inconsistency for array slot 0" errors). The cause is still being investigated with
the standalone probes in `tools/tlsprobe/`. Do not build with `-fno-emulated-tls`.

## Other

- `thread_local` destructors did not run for threads that end by returning normally, in the test above.
  It is not yet known whether this also happens with emulated TLS.
- The console's own screenshot feature captures a black image while KB-RPCS3 is open. L3 + R3 on the
  KB-RPCS3 home screen saves a screenshot to `rpcs3/screenshots/`.
- Home-screen tiles appear only after KB-RPCS3 is closed (ShadowMountPlus does not scan while a game
  runs).
- This is an alpha: expect crashes. Keep a copy of a build that works for you (`ROLLBACK.md`).
