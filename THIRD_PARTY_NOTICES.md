# Third-party notices

KB-RPCS3 is built on the work of many people. The copyright notices and licence headers in the source
files are authoritative; this file summarises them. Licence texts are in `LICENSES/`.

## Projects this release contains or patches

| Project | Authors | Licence | Where |
|---|---|---|---|
| **RPCS3** — PlayStation 3 emulator (https://rpcs3.net, https://github.com/RPCS3/rpcs3) | the RPCS3 team and contributors | GPL-2.0-only | patched by `port/rpcs3-patches/` (not included; fetched from upstream) |
| **PS5 Vulkan Template** (https://github.com/mihawk-99/PS5_VulkanTemplate) | Mihawk | MIT (base, PS5 layer) and GPL-3.0-or-later (UI module, title program) | `app/` |
| **Vulkan examples base** | Sascha Willems | MIT (`LICENSES/MIT-SaschaWillems.txt`) | `app/base/` |
| GLM, tinygltf, KTX, Dear ImGui, Vulkan headers, volk | their authors | MIT / Apache-2.0 (see each folder's licence file) | `app/external/`, `app/ps5/third_party/` |

## Projects needed to build (fetched separately, not included)

| Project | Authors | Licence |
|---|---|---|
| **PS5_Vulkan** (RADV/Mesa for PS5, link recipe, native tool, `libc.prx`) | Mihawk; Mesa contributors | GPL-3.0 (Mesa/RADV: MIT) |
| **ps5-payload-sdk** (https://github.com/ps5-payload-dev/sdk) and its fork with the PS5 platform layer | ps5-payload-dev; Mihawk | GPL-3.0-or-later |
| **ps5-homebrew-ui** (https://github.com/blackbearreloaded/ps5-homebrew-ui), via Mihawk's PS5_VKHomebrewUI fork | BlackBearReloaded; Mihawk | GPL-3.0 (sounds and music: GPL-3.0-or-later) |
| LLVM runtimes | LLVM contributors | Apache-2.0 WITH LLVM-exception |
| FFmpeg | FFmpeg developers | LGPL-2.1-or-later / GPL-2.0-or-later (build-dependent) |
| zlib, libiconv | their authors | Zlib, LGPL |

## Fonts the title uses (from the UI kit's asset set)

| Font | Authors | Licence |
|---|---|---|
| Roboto Medium | Christian Robertson (Google Fonts) | Apache-2.0 |
| Inter, Montserrat, Press Start 2P, Patrick Hand | their project authors | OFL-1.1 |
| DejaVu Sans Mono | Bitstream, Inc.; DejaVu changes public domain | Bitstream-Vera |

## Data

- The title's per-game recommended settings use RPCS3's public configuration database
  (api.rpcs3.net), © the RPCS3 project. It is **not** included in this release; `BUILDING.md` explains
  how to fetch it. Without it the title works and simply offers no recommendations.
- Box art for home-screen tiles is downloaded at run time from GameTDB (https://www.gametdb.com) and is
  not part of this release.

## Trade marks

"PlayStation", "PS3" and "PS5" are trade marks of Sony Interactive Entertainment Inc. RPCS3 is the name
of the RPCS3 project. KB-RPCS3 is not affiliated with, endorsed or sponsored by Sony Interactive
Entertainment or the RPCS3 project. No Sony logo, firmware, key or system file is included.
