# Installing KB-RPCS3

> **Alpha 0.1.0 has no downloadable build.** These steps are for a build you made yourself from source
> (`BUILDING.md`), for your own use. Do not share builds: see `LICENSING_STATUS.md`.

## What the console needs

KB-RPCS3 was developed and tested with this setup. Other firmware versions or tools may work but are
untested.

| Requirement | Used during development |
|---|---|
| Jailbroken PS5 | PS5 Slim, firmware 13.60 |
| Homebrew enabler | etaHEN (FTP server on port 1337, ELF loader on port 9021) |
| Kernel extension | kstuff lite, loaded **once** per console boot (loading it twice crashed the console) |
| Folder-title loader | [ShadowMountPlus](https://github.com/drakmor/ShadowMountPlus): registers titles in `/data/homebrew/` on the home screen |
| Your own PS3 system software | `PS3UPDAT.PUP`, from Sony's PS3 update page |
| Your own games | PS3 discs as `.iso` or folders; PSN games as `.pkg` + `.rap` |

KB-RPCS3 does not include any of these.

## Install a build

1. Copy your built `dist/PPSA99303/` folder to the console as `/data/homebrew/PPSA99303/` (for example
   with an FTP client connected to etaHEN's FTP server).
2. Wait for ShadowMountPlus to register it. **KB-RPCS3** appears on the PS5 home screen. If it doesn't,
   restart the console.
3. Start KB-RPCS3. Put `PS3UPDAT.PUP` in `/data/homebrew/PPSA99303/rpcs3/`, then use
   **Install → PS3 firmware**.
4. Add games to `/data/homebrew/PPSA99303/rpcs3/`:
   - disc games: `games/` (an `.iso` file or a game folder);
   - PSN games: the `.pkg` files in `packages/` and the `.rap` licence files in `exdata/`, then
     **Install → Packages**.
5. Pick a game on the Games tab and press Cross. Hold the touch pad for 2 seconds to leave a game.

## Normal game speed

Per-game settings are stored in `rpcs3/custom_configs/`. Normal play needs **Clocks scale: 100** and
**Frame limit: Auto**. Other values change how fast games run.

## Update or remove

- **Update:** close KB-RPCS3, then replace the files in `/data/homebrew/PPSA99303/`. Keep the `rpcs3/`
  folder to keep your settings, games and saves.
- **Remove:** delete the KB-RPCS3 icon on the home screen (Options → Delete), then delete
  `/data/homebrew/PPSA99303/`. This also deletes your games and saves inside it.
