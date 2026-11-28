<p align="center">
  <img src="assets/icon/kamo-icon.png" width="128" alt="KAMO icon">
</p>

<h1 align="center">KAMO</h1>

<p align="center">
  <b>KAMO = Kirby &amp; The Amazing Mirror + Online</b><br>
  The game recompiled to run natively on PC, with online co-op for up to four players.
</p>

<p align="center">
  <img alt="Status: alpha" src="https://img.shields.io/badge/status-alpha-orange">
  <img alt="Platform: Windows" src="https://img.shields.io/badge/platform-Windows-blue">
  <img alt="License: MIT (KAMO sources)" src="https://img.shields.io/badge/license-MIT-green">
</p>

> [!IMPORTANT]
> KAMO does not include the game. You need your own legally dumped ROM of **Kirby & The Amazing
> Mirror (USA)**; no BIOS is needed. KAMO is an unofficial fan project, not
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
| Continue an online match | ✅ |
| Installer and automatic updates | ✅ |
| Coverage of every level, boss and sub-game in multiplayer | 🔨 In progress |
| macOS and Linux builds | 📋 Planned |

See the [roadmap](docs/ROADMAP.md) for details.

## Playing

1. Download `KAMO-Setup-<version>.exe` from the
   [latest release](https://github.com/OmegaElMegadivo/KAMO/releases/latest) and run it. It asks for your
   ROM once: only the USA release, SHA-1 `274b102b6d940f46861a92b4e65f89a51815c12c`, is accepted,
   and KAMO only remembers where the file is. (A portable zip is there too; it asks for the ROM
   the first time it starts.)
2. KAMO opens on its menu: **Play alone**, **Play online** or **Options**.
3. In the game, **Esc** (or Start + Select on a gamepad) opens the game menu; **Back to main
   menu** there, or closing the window, returns to KAMO's menu. **Options > Shortcuts** lists
   the other keys.

KAMO checks for a new version when it starts and offers to update itself. It also works from
[RecompLauncher](https://github.com/elripalda/RecompLauncher): the release zip carries a
`recomplauncher.json`, and the launcher hands KAMO the ROM and installs the updates.

### Online

1. **Play online**. The first time, sign in with Discord (the lobby server needs it; KAMO
   remembers it). Then create a room (public, or private with a password) for
   two to four players, or join one from the list.
2. Everyone presses **I'm ready**; the host presses **Start match**. In the game choose
   **Start Game > Multi Player**; player 1 confirms.
3. Close the game window to save. To go on later, everyone ticks **Continue the last saved match**
   in the room before the host starts.

**Input lag.** The host's input delay is chosen from the slowest ping in the room. If you want
less lag on a good connection, set it in **Options > Online** (2 or 3 frames); raise it if the
game stutters.

Everyone needs the same KAMO version and the same ROM. Online progress is kept in the shared match,
not in each player's single-player save, and a match saved with one KAMO version can only be
continued with that version.

How online play works is described in [docs/MULTIPLAYER.md](docs/MULTIPLAYER.md).

## Building

See [`recomp/README.md`](recomp/README.md). The build recompiles your own ROM locally; nothing
generated from it is committed.

## Project layout

| Path | Contents |
|---|---|
| `recomp/` | The game host and KAMO's menu, the game configuration (`game.toml`), KAMO's patches to GBARecomp and recomp-ui, installer and package files |
| `external/gbarecomp` | GBARecomp: recompiler and runtime (submodule) |
| `external/recomp-ui` | recomp-ui: online lobby backend, in-game menu, Dear ImGui (submodule) |
| `tools/` | Build script with a RAM watchdog, the test suite and the packaging script |
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
- [recomp-ui](https://github.com/mstan/recomp-ui) (MIT): the online lobby, the in-game menu and its
  copy of [Dear ImGui](https://github.com/ocornut/imgui) (MIT), used for KAMO's menu.
- [Inno Setup](https://jrsoftware.org/isinfo.php): builds the installer.
- [KNID](https://github.com/sweatyeggs69/KNID) by sweatyeggs69 (MIT): KAMO versions up to 0.1.x
  were built on it.
- [SDL](https://libsdl.org) and [zlib](https://zlib.net).

Full license texts are in [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

## License

KAMO's own sources are released under the [MIT License](LICENSE). Builds include GBARecomp and
are therefore limited to noncommercial use (see [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)).
