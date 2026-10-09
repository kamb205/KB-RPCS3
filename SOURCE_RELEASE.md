# Source release — KB-RPCS3 Alpha 0.1.0

`KB-RPCS3-0.1.0-Source.tar.gz` contains this repository at the `v0.1.0-alpha` tag.

## Layout

| Path | Contents | Licence |
|---|---|---|
| `port/rpcs3-patches/` | 47 patches for upstream RPCS3 revision `46aee28f8` | GPL-2.0-only |
| `port/apply-rpcs3-patches.sh` | clones RPCS3 and applies the series | GPL-2.0-only |
| `port/*.sh`, `port/*.py` | build helper scripts | GPL-3.0-or-later |
| `app/` | the PS5 title shell | see `app/LICENSING.md` |
| `tools/` | console helper payloads and scripts, standalone TLS probes | per file header |
| `assets/branding/` | original KB-RPCS3 logo, banner, social-preview and release artwork, with SVG sources and render script | GPL-3.0-or-later |
| `.github/` | issue and pull-request templates | — |
| `LICENSES/`, `COPYING` | licence texts | — |
| root `*.md` | documentation (`README`, `BUILDING`, `INSTALL`, `LICENSING_STATUS`, `KNOWN_ISSUES`, `COMPATIBILITY`, `THIRD_PARTY_NOTICES`, `CONTRIBUTING`, `SECURITY`, `CODE_OF_CONDUCT`, `ROADMAP`, `PERMISSION_REQUESTS`, `ROLLBACK`, release notes, this file) | — |

## The RPCS3 patch series

- Patches 1–46 are the port's development commits, unchanged except for the author name.
- Patch 47 combines the Alpha 0.1.0 work: the experimental SPU options (off by default), speed and
  timing logging, and build fixes.
- Left out from development history: an accidental change that replaced RPCS3's `3rdparty` submodules
  with local symbolic links, and a native-TLS self-test that only makes sense in the failed native-TLS
  build.

## Verification done for this release (2026-10-09)

- All 47 patches applied with `git am --keep-cr` to a clean checkout of RPCS3 `46aee28f8`. The resulting
  source tree is identical to the tree the series was exported from, and all 28 `3rdparty` submodules
  are intact.
- Patches 1–46 reproduce the source of the build tested on the console byte for byte.
- Patch 47 equals the development source of the r3 build minus the native-TLS self-test. The one file
  changed by that removal (`rpcs3/ps5/ps5_frontend.cpp`) was compiled with the r3 build's compiler
  command and builds cleanly. The complete Alpha 0.1.0 source has **not** been rebuilt end to end.
- No patch or file contains personal names, e-mail addresses, home-directory paths or private network
  addresses. Archive entries carry neutral owner names.

## Presentation update (2026-10-09)

Repository presentation was added on top of the source release: the README, the community files
(`CONTRIBUTING.md`, `SECURITY.md`, `CODE_OF_CONDUCT.md`, `ROADMAP.md`), the `.github/` templates and the
original `assets/branding/` artwork. These are documentation, configuration and graphics only — **no
patch, source or build input changed**, so the patch-series verification above still holds. The
distribution archive is regenerated from the `v0.1.0-alpha` tag after these changes, and its SHA-256 is
published with the release.
