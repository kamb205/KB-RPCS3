# Rolling back — Alpha 0.1.0

Keep the previous working title folder before replacing it.

## Keep a rollback copy

1. Before installing a new build, copy the current title folder (e.g. `PPSA99303/`) somewhere safe,
   outside the console's title directory.
2. Note its `eboot.bin` size or hash so you can identify it.

## Revert

1. Close any running game/app on the console.
2. Replace the installed title folder with your saved copy.
3. Restart the console if the home-screen tile does not reappear.

## Restore normal-speed configuration

If a game was left on the benchmark configuration, set its per-game config back to
**Clocks scale: 100** and **Frame limit: Auto**, or delete the per-game config so the global one applies.
The benchmark scripts do this automatically on exit, but a manual edit is harmless.

## Development rollback points

During development, working builds were preserved as full title-folder backups and git tags. If you
built your own copy, keep at least one known-good `eboot.bin` and its matching configuration.
