# KAMO on GBARecomp

This folder builds KAMO as a static recompilation on the
[GBARecomp](https://github.com/mstan/gbarecomp) runtime (submodule in `external/gbarecomp`).
The game's machine code is translated to C++ from your own ROM; the result runs natively,
with no CPU interpreter.

Nothing generated from the ROM is ever committed: `generated/` and `build-*/` are git-ignored.
No BIOS is needed: the runtime boots the cartridge directly and services the BIOS calls natively,
in single player and online (`KAMO_WITH_BIOS=ON` links a recompiled retail BIOS instead, for
development only).

## Requirements (Windows)

- Python 3.11+
- MSYS2 with the MINGW64 toolchain:
  `pacman -S --needed mingw-w64-x86_64-toolchain mingw-w64-x86_64-cmake mingw-w64-x86_64-ninja mingw-w64-x86_64-SDL2 mingw-w64-x86_64-zlib`
- Your own dump of Kirby & The Amazing Mirror (USA), SHA-1 `274b102b6d940f46861a92b4e65f89a51815c12c`

## Build

From the repository root, with `C:\msys64\mingw64\bin` first on `PATH`:

```powershell
git submodule update --init --recursive external/gbarecomp external/recomp-ui

# 1. The recompiler
cmake -S external/gbarecomp -B build-gbarecomp -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build-gbarecomp --target gba_recompile

# 2. Recompile the game (local output only)
python external/gbarecomp/tools/cli.py build --rom <kirby.gba> --output generated/b8ke --config recomp/game.toml --codegen-shards 64

# 3. The game
cmake -S recomp -B build-recomp -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build-recomp --target KAMO -j 6   # the generated code needs ~2 GB RAM per job
```

`tools/cli.py` looks for the recompiler at
`external/gbarecomp/build/cli-release/core/Release/gba_recompile.exe`; copy it there after step 1.

## Run

Start `KAMO.exe` with no arguments to open KAMO's menu (`recomp/src/kamo_frontend.cpp`): the
first time it asks for the ROM, then offers Play alone, Play online and Options. Settings are
kept next to the executable in `kamo.ini`, `rom.cfg` and `keybinds.ini`.
`--rom <kirby.gba>` (or `--no-menu`) boots straight into the game.

## Online

**Play online** uses rooms on the public lobby server (recomp-ui's lobby backend): create one
(public, or private with a password) for two to four players, or join one from the list.
Everyone needs the same KAMO version and the same ROM; rooms of another version are shown but
cannot be joined. The host's input delay is chosen from the slowest ping, or set in Options.

### Direct IP from the command line (up to four players)

Everyone uses the same session number. Player 1 hosts; the others connect to player 1:

```powershell
# Player 1 (host)
KAMO.exe --rom <rom> --netplay-players 4 --netplay-seat 0 --netplay-session 123 --netplay-bind 0.0.0.0:7845
# Players 2-4 (seat 1, 2, 3)
KAMO.exe --rom <rom> --netplay-players 4 --netplay-seat 1 --netplay-session 123 --netplay-bind 0.0.0.0:7846 --netplay-peer <host-ip>:7845
```

Closing the game window asks every player to save the match together; the shared archive
goes to `netplay/session-<ID>.paired` (`--netplay-checkpoint` overrides it). To continue,
every player ticks **Continue the last saved match** in the room, or starts KAMO with
`--continue-online` (the newest archive) or `--netplay-resume <file>` with a new session number.

Three or four players use delay-sync by default, two players rollback. `--netplay-delay 2..20`
sets the input delay (default 6). Every player needs a build from the same sources and the
same ROM; the session refuses mismatches before the first frame.

## Testing linked play

`KAMOViewProbe` (built with `cmake --build build-recomp --target KAMOViewProbe`) runs 2-4
linked consoles headless from a button script. Each script line is
`<frame> <P1> <P2> [<P3> <P4>]` with GBA key masks (A=1 B=2 Select=4 Start=8 Right=16 Left=32
Up=64 Down=128 R=256 L=512); a line holds until the next one.

```powershell
# Cold boot four linked consoles for 6000 frames; writes <out>.state and a screenshot per seat
# ("none" in place of a BIOS file boots every console without one)
build-recomp/KAMOViewProbe.exe <rom> none cold <out> 4 6000 <script>
# Continue from that state as four networked peers with 40 ms latency / 10 ms jitter
python external/gbarecomp/tests/link/view_network_loopback.py build-recomp/KAMOViewProbe.exe `
  <rom> none <out>.state <dir> --inputs <script> --frames 1800 --widths 240,240,240,240
```

The second command passes only if all four peers finish with identical state.

Code the game reaches but the recompiler missed is run by an interpreter fallback in
single player and logged to `recomp_master_misses_B8KE.toml.frag` next to the executable.
Review it, add genuine entries to `game.toml` and regenerate. Netplay refuses the fallback, so
linked play needs that coverage.

## Packaging

`tools/kamo_package.ps1 -BuildDir build-recomp` writes to `dist/`: the `KAMO` folder, the
portable zip, the installer `KAMO-Setup-<version>.exe` (Inno Setup 6, `recomp/package/kamo-setup.iss`)
and `SHA256SUMS.txt`. The version comes from `project()` in `recomp/CMakeLists.txt`. Publish the
installer with every release: installed copies update themselves from the latest GitHub release
(`recomp/src/kamo_update.cpp`). Never include a ROM, BIOS, save or anything from `generated/`.

## License

GBARecomp is distributed under the PolyForm Noncommercial License 1.0.0, so builds of KAMO
that include it may only be used and shared noncommercially. KAMO's own sources stay MIT.
