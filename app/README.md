# RPCS3 PS5

A PS5 homebrew title (`PPSA99303`) on RADV, made from the
[PS5 Vulkan Template](https://github.com/mihawk-99/PS5_VulkanTemplate) foundation at
`fdd41492` with its UI module: Sascha Willems' Vulkan example base class with its PS5
hooks, the PS5 layer (the launch, the pad, sound, klog, test runs, the build and the
console tools), BlackBearReloaded's UI kit drawn with Vulkan (through my fork
PS5_VKHomebrewUI), and one program, `examples/rpcs3ps5/rpcs3ps5.cpp`, whose screen is its
own copy of the kit's "aurora" design, `examples/rpcs3ps5/kit/screen.cpp`.

## Building and running

It builds against my PS5 stack, checked out beside it: PS5_Vulkan (the RADV release
archive, the link recipe, the native tool and `libc.prx`), the payload SDK fork
(`ps5/tools/setup-sdk.sh` installs it at the pinned revision) and PS5_VKHomebrewUI
(`ps5/ui/setup-kit.sh` exports the kit at the pinned revision, from GitHub when it is
not beside it).

```bash
ps5/tools/build.sh              # dist/PPSA99303/: eboot.bin, sce_sys, assets (the kit's fonts and sounds)
ps5/tools/deploy.sh             # upload what changed, over the console's FTP server
ps5/tools/run.sh                # a test run: 300 frames, the last one saved and checked
ps5/tools/run.sh --menu         # no test: the program, until it ends itself
ps5/tools/host-reference.sh     # the same frames on this PC's Vulkan driver
```

A launch from the home screen shows the screen at once. Every button is the
design's; holding OPTIONS for a second ends the title.

## Growing it

- **The screen** is `examples/rpcs3ps5/kit/screen.cpp`: a class with `update(input, dt,
  feedback)` and a const `draw(frame)`, assembled from the kit's components and
  themes. The kit's guides are in PS5_VKHomebrewUI's `docs/` (CRAFT.md first, then
  COMPONENTS.md, THEMES.md and KIT.md), its headers in `.deps/hui/src/`.
- **The program** around it, `examples/rpcs3ps5/rpcs3ps5.cpp`, owns the frame: input, sound,
  the light bar, the layers. A 3D scene goes under the screen as in the template's
  `uioverlay` sample (`kit.renderer.import_texture`).
- **Assets** go in `ps5/assets.json`, each with its origin and licence; only assets
  with a clear licence are shipped (`ps5/ASSETS.md` is written from it).
- How the foundation and the UI module work: PS5_VulkanTemplate's `ps5/README.md`
  and `ps5/ui/README.md`; the agent skills for this stack are in its `skills/`.

## Licences

The UI kit is GPL-3.0-or-later, and so are this title's program, its screen, the UI
module (`ps5/ui/`) and the kit's sounds (`ps5/licenses/GPL-3.0.txt`); the kit's fonts
are OFL-1.1 and Bitstream Vera (`ps5/ASSETS.md`). The rest of the foundation is MIT
(`LICENSE.md`, Sascha Willems'; the PS5 layer is mine, under the same licence). The
title as distributed is under GPL-3.0.
