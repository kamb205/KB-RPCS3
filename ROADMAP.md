# Roadmap

This is a direction, not a schedule or a promise. KB-RPCS3 is a small, unofficial
alpha project, and everything here depends on time and on permissions from the
upstream authors it builds on.

## Now — make a lawful binary possible

- Resolve the **GPL-2.0-only / GPL-3.0** conflict so a PS5 package can be distributed.
  The realistic route is making every linked non-RPCS3 component available under
  GPL-2.0-compatible terms; requests are drafted in
  [PERMISSION_REQUESTS.md](PERMISSION_REQUESTS.md) and the reasoning is in
  [LICENSING_STATUS.md](LICENSING_STATUS.md).

## Next — finish validating Alpha 0.1.0

- Run the **exact Alpha 0.1.0 source and branding** through console regression testing
  (the tested build predates the branding and the logging additions).
- Capture **genuine screenshots** on real hardware and publish them.
- Publish accurate, normal-speed performance results for the tested games.

## Then — performance and reliability

- Validate the **experimental SPU options** (LLVM insert-select, hot-SPU pinning,
  narrow local-store stores) at normal game speed, and only then consider enabling any
  of them.
- Reduce first-run stutter from SPU/JIT and shader compilation.
- Investigate **native TLS** on the PS5, which currently does not work
  ([KNOWN_ISSUES.md](KNOWN_ISSUES.md)); the standalone probes live in `tools/tlsprobe/`.

## Later — reach and reproducibility

- Broaden **game compatibility testing** and keep the compatibility table honest.
- Make the **build reproducible and portable**, moving away from fixed toolchain paths
  to a documented, scriptable setup.

## Out of scope

- Distributing Sony firmware, games, keys or any other Sony content.
- Claiming or implying affiliation with Sony Interactive Entertainment or the RPCS3
  project.
