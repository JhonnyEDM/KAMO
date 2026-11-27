<p align="center">
  <img src="assets/icon/kamo-icon.png" width="128" alt="KAMO icon">
</p>

<h1 align="center">KAMO</h1>

<p align="center">
  <b>Kirby &amp; The Amazing Mirror, recompiled to run natively on PC — with online co-op for up to four players.</b>
</p>

<p align="center">
  <img alt="Status: alpha" src="https://img.shields.io/badge/status-alpha-orange">
  <img alt="Platform: Windows" src="https://img.shields.io/badge/platform-Windows-blue">
  <img alt="License: MIT (KAMO sources)" src="https://img.shields.io/badge/license-MIT-green">
</p>

> [!IMPORTANT]
> KAMO does not include the game. You need your own legally dumped ROM of **Kirby & The Amazing
> Mirror (USA)** and your own Game Boy Advance BIOS. KAMO is an unofficial fan project, not
> affiliated with, endorsed or sponsored by Nintendo or HAL Laboratory. Kirby and all related
> names are trademarks of their respective owners.

## About

Kirby & The Amazing Mirror was built around four-player co-op, but playing it that way has always
meant four Game Boy Advance systems, four cartridges and link cables. KAMO brings the game to PC
and replaces the link cable with the internet: create a room, invite your friends or open it to
everyone, and explore the Mirror World together.

The game's machine code is translated to C++ ahead of time from your own ROM (static
recompilation with [GBARecomp](https://github.com/mstan/gbarecomp)) and runs natively; there is
no CPU emulator. The Game Boy Advance hardware around it (graphics, sound, timers, link cable) is
reproduced by the GBARecomp runtime.

## Status

| | |
|---|---|
| Single player | ✅ Playable |
| Online co-op, 2 players (public and private rooms) | ✅ Tested between two PCs |
| Online co-op, 3–4 players | 🧪 Testing |
| Continue an online match | ✅ `--continue-online` |
| Coverage of every level, boss and sub-game in multiplayer | 🔨 In progress |
| macOS and Linux builds | 📋 Planned |

See the [roadmap](docs/ROADMAP.md) for details.

## Playing

1. Download `KAMO-<version>-Windows-x64.zip` from the
   [latest release](https://github.com/JhonnyEDM/KAMO/releases/latest), extract it and run `KAMO.exe`.
2. In the launcher, pick your ROM (**Browse For ROM**). Only the USA release, SHA-1
   `274b102b6d940f46861a92b4e65f89a51815c12c`, is supported. KAMO only remembers where the file is.
3. Open **Settings** and choose your GBA BIOS (`gba_bios.bin`). **Play** stays disabled until
   both the ROM and the BIOS are set.
4. Press **Play**.

### Online

1. Press **Netplay**. One player creates a room (public, or private with a password); the others
   join it from the list. Online rooms hold up to four players; LAN / direct-IP rooms hold two.
2. When everyone is in, the host presses **Play**. On every console choose
   **Start Game > Multi Player**; player 1 confirms.
3. Close the game window to save. Every player saves the same shared match in the `netplay`
   folder next to `KAMO.exe`.
4. To go on later, every player starts **`Continue online match.cmd`** (or
   `KAMO.exe --continue-online`) and you meet in a room as before.

**Input lag.** The host sets the input delay for everyone, chosen from the ping when the match
starts. With a low ping, open **Settings** in the room, turn on **Manual Input Delay** and try 2
or 3 frames; raise it if the game stutters.

Everyone needs the same KAMO version and the same ROM. Online progress is kept in the shared match,
not in each player's single-player save, and a match saved with one KAMO version can only be
continued with that version.

How online play works is described in [docs/MULTIPLAYER.md](docs/MULTIPLAYER.md).

## Building

See [`recomp/README.md`](recomp/README.md). The build recompiles your own ROM and BIOS locally;
nothing generated from them is committed.

## Project layout

| Path | Contents |
|---|---|
| `recomp/` | The game host, its game configuration (`game.toml`), KAMO's patches to GBARecomp, package files |
| `external/gbarecomp` | GBARecomp: recompiler and runtime (submodule) |
| `external/recomp-ui` | recomp-ui: launcher window and online lobby (submodule) |
| `tools/` | Build script with a RAM watchdog, and the test suite |
| `docs/` | Multiplayer design and roadmap |

## Contributing

Bug reports and pull requests are welcome; see [CONTRIBUTING.md](CONTRIBUTING.md). Keep game
content out of the repository: no ROMs, BIOS files, saves, save states, extracted assets, code
generated from the ROM, or screenshots of the game.

## Credits

- [GBARecomp](https://github.com/mstan/gbarecomp) by Matthew Stanley, used under the PolyForm
  Noncommercial License 1.0.0, with its components
  [arm-recomp-core](https://github.com/mstan/arm-recomp-core),
  [recomp-net](https://github.com/RetroPortingToolKit/recomp-net) and
  [rbengine](https://github.com/RetroPortingToolKit/rbengine) (MIT).
- [recomp-ui](https://github.com/mstan/recomp-ui) (MIT): the launcher and the online lobby.
- [KNID](https://github.com/sweatyeggs69/KNID) by sweatyeggs69 (MIT): KAMO versions up to 0.1.x
  were built on it.
- [SDL](https://libsdl.org) and [zlib](https://zlib.net).

Full license texts are in [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

## License

KAMO's own sources are released under the [MIT License](LICENSE). Builds include GBARecomp and
are therefore limited to noncommercial use (see [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)).
