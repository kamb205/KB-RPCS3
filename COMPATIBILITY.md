# Compatibility — Alpha 0.1.0

## Tested setup

One PS5 Slim on firmware 13.60, with etaHEN, kstuff lite and ShadowMountPlus. Nothing else has been
tested.

## Games

Tested with the development build that Alpha 0.1.0 is based on (emulated TLS, experimental options off),
at normal game speed (Clocks scale 100, Frame limit Auto):

| Game | Serial | Result |
|---|---|---|
| LIMBO | HPEB00564 | Playable: picture, sound, controls. |
| WWE 12 | BLES01439 | Playable. Menus run near 60 FPS; matches slow down during big moves (CPU-limited). |
| Grand Theft Auto IV | BLES01128 | Playable. Cutscenes at the game's own 30 FPS cap; roughly 13–27 FPS when driving, with occasional audio stutter. Needs "SPU XFloat Accuracy: Accurate" (part of the recommended settings) or cars fall through the road. |

GTA IV is limited by how fast the console's CPU runs the game's SPU code, not by the graphics.

Every other PS3 game is **untested** on KB-RPCS3. RPCS3's own compatibility list
(https://rpcs3.net/compatibility) is a hint, but the PS5's CPU is much slower than the PCs that list is
based on.

## About older FPS numbers

During development, some GTA IV benchmarks ran with **Clocks scale 300 and Frame limit Off** so that the
game's 30 FPS cap would not hide the CPU cost. In that mode the game runs about three times too fast, so
those numbers (around 20–22 FPS) are **not** normal-play results and should not be quoted as such.
