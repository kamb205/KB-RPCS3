# Assets

The assets the title ships, written from `ps5/assets.json` by
`ps5/tools/build-assets.py --notices`. Only assets with a clear licence are
shipped: in PS5_VulkanTemplate, the asset pack's other files (the `assets`
submodule) stay out, and the samples that use them get the replacements here.

| Path under `/app0/assets/` | Asset | Author | Licence | Samples |
| --- | --- | --- | --- | --- |
| `Roboto-Medium.ttf` | [Roboto Medium](https://fonts.google.com/specimen/Roboto) | Christian Robertson (Google Fonts) | Apache-2.0 | all |
| `hui/fonts/inter-regular.huifont` | [Inter Regular](https://github.com/rsms/inter) (a signed-distance-field atlas, made by ps5-homebrew-ui's tools/bake-fonts.sh) | The Inter Project Authors | OFL-1.1 | the UI module |
| `hui/fonts/inter-semibold.huifont` | [Inter SemiBold](https://github.com/rsms/inter) (a signed-distance-field atlas, made by ps5-homebrew-ui's tools/bake-fonts.sh) | The Inter Project Authors | OFL-1.1 | the UI module |
| `hui/fonts/montserrat-medium.huifont` | [Montserrat Medium](https://github.com/JulietaUla/Montserrat) (a signed-distance-field atlas, made by ps5-homebrew-ui's tools/bake-fonts.sh) | The Montserrat Project Authors | OFL-1.1 | the UI module |
| `hui/fonts/dejavu-sans-mono.huifont` | [DejaVu Sans Mono](https://dejavu-fonts.github.io/) (a signed-distance-field atlas, made by ps5-homebrew-ui's tools/bake-fonts.sh) | Bitstream, Inc.; the DejaVu changes are in the public domain | Bitstream-Vera | the UI module |
| `hui/fonts/press-start-2p.huifont` | [Press Start 2P](https://fonts.google.com/specimen/Press+Start+2P) (a signed-distance-field atlas, made by ps5-homebrew-ui's tools/bake-fonts.sh) | The Press Start 2P Project Authors | OFL-1.1 | the UI module |
| `hui/fonts/patrick-hand.huifont` | [Patrick Hand](https://fonts.google.com/specimen/Patrick+Hand) (a signed-distance-field atlas, made by ps5-homebrew-ui's tools/bake-fonts.sh) | Patrick Wagesreiter | OFL-1.1 | the UI module |
| `hui/audio/sfx/glass` | [The glass sound set (ProsperoEden's sound effects)](https://github.com/blackbearreloaded/ps5-homebrew-ui) (generated with ElevenLabs Sound Effects v2 for ProsperoEden, trimmed, faded and levelled by ps5-homebrew-ui's tools/process-sfx.py (its THIRD_PARTY_NOTICES.md)) | BlackBearReloaded | GPL-3.0-or-later | the UI module |
| `hui/audio/sfx/paper` | [The paper sound set (ProsperoPuzzles' sound effects)](https://github.com/blackbearreloaded/ps5-homebrew-ui) (generated with ElevenLabs Sound Effects v2 for ProsperoPuzzles, trimmed, faded and levelled by ps5-homebrew-ui's tools/process-sfx.py (its THIRD_PARTY_NOTICES.md)) | BlackBearReloaded | GPL-3.0-or-later | the UI module |
| `hui/audio/music` | [First Light, Open Strings and Quiet Hours](https://github.com/blackbearreloaded/ps5-homebrew-ui) (48 kHz OGG Vorbis at -18 LUFS, as ps5-homebrew-ui ships them) | BlackBearReloaded | GPL-3.0-or-later | the UI module |
| `hui/fonts/Inter-Regular.ttf` | [Inter Regular](https://github.com/rsms/inter) (the TrueType file, for the samples' settings windows in the title's theme) | The Inter Project Authors | OFL-1.1 | the UI module |
| `hui/fonts/Inter-SemiBold.ttf` | [Inter SemiBold](https://github.com/rsms/inter) (the TrueType file, for the samples' settings windows in the title's theme) | The Inter Project Authors | OFL-1.1 | the UI module |
| `hui/fonts/DejaVuSansMono.ttf` | [DejaVu Sans Mono](https://dejavu-fonts.github.io/) (the TrueType file, for the samples' settings windows in the title's theme) | Bitstream, Inc.; the DejaVu changes are in the public domain | Bitstream-Vera | the UI module |
| `hui/fonts/PressStart2P-Regular.ttf` | [Press Start 2P](https://fonts.google.com/specimen/Press+Start+2P) (the TrueType file, for the samples' settings windows in the title's theme) | The Press Start 2P Project Authors | OFL-1.1 | the UI module |
| `hui/fonts/PatrickHand-Regular.ttf` | [Patrick Hand](https://fonts.google.com/specimen/Patrick+Hand) (the TrueType file, for the samples' settings windows in the title's theme) | Patrick Wagesreiter | OFL-1.1 | the UI module |
