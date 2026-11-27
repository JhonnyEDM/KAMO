# KAMO on GBARecomp

This folder builds KAMO as a static recompilation on the
[GBARecomp](https://github.com/mstan/gbarecomp) runtime (submodule in `external/gbarecomp`).
The game's machine code is translated to C++ from your own ROM; the result runs natively,
with no CPU interpreter.

Nothing generated from the ROM or BIOS is ever committed: `generated/` and `build-*/` are
git-ignored.

## Requirements (Windows)

- Python 3.11+
- MSYS2 with the MINGW64 toolchain:
  `pacman -S --needed mingw-w64-x86_64-toolchain mingw-w64-x86_64-cmake mingw-w64-x86_64-ninja mingw-w64-x86_64-SDL2 mingw-w64-x86_64-zlib`
- Your own dump of Kirby & The Amazing Mirror (USA), SHA-1 `274b102b6d940f46861a92b4e65f89a51815c12c`
- Your own retail GBA BIOS

## Build

From the repository root, with `C:\msys64\mingw64\bin` first on `PATH`:

```powershell
git submodule update --init --recursive external/gbarecomp

# 1. The recompiler
cmake -S external/gbarecomp -B build-gbarecomp -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build-gbarecomp --target gba_recompile

# 2. Recompile the BIOS and the game (local output only)
build-gbarecomp/gba_recompile.exe --bios <gba_bios.bin> --config external/gbarecomp/bios/gba_bios.toml --out generated/bios
python external/gbarecomp/tools/cli.py build --rom <kirby.gba> --output generated/b8ke --codegen-shards 16

# 3. The game
cmake -S recomp -B build-recomp -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build-recomp --target KAMO
```

`tools/cli.py` looks for the recompiler at
`external/gbarecomp/build/cli-release/core/Release/gba_recompile.exe`; copy it there after step 1.

## Run

```powershell
build-recomp/KAMO.exe --rom <kirby.gba> --bios <gba_bios.bin>
```

## Online (direct IP, up to four players)

Everyone uses the same session number. Player 1 hosts; the others connect to player 1:

```powershell
# Player 1 (host)
KAMO.exe --rom <rom> --bios <bios> --netplay-players 4 --netplay-seat 0 --netplay-session 123 --netplay-bind 0.0.0.0:7845
# Players 2-4 (seat 1, 2, 3)
KAMO.exe --rom <rom> --bios <bios> --netplay-players 4 --netplay-seat 1 --netplay-session 123 --netplay-bind 0.0.0.0:7846 --netplay-peer <host-ip>:7845
```

Closing the game asks every player to save the session together; the shared checkpoint
goes to `netplay/session-<ID>.paired` (`--netplay-checkpoint` overrides it). Resume with
`--netplay-resume <file>` and a new session number.

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
build-recomp/KAMOViewProbe.exe <rom> <bios> cold <out> 4 6000 <script>
# Continue from that state as four networked peers with 40 ms latency / 10 ms jitter
python external/gbarecomp/tests/link/view_network_loopback.py build-recomp/KAMOViewProbe.exe `
  <rom> <bios> <out>.state <dir> --inputs <script> --frames 1800 --widths 240,240,240,240
```

The second command passes only if all four peers finish with identical state.

Code the game reaches but the recompiler missed is run by an interpreter fallback in
single player and logged to `recomp_master_misses_B8KE.toml.frag` next to the executable.
Review it, add genuine entries to `game.toml` and regenerate. Netplay refuses the fallback, so
linked play needs that coverage.

## License

GBARecomp is distributed under the PolyForm Noncommercial License 1.0.0, so builds of KAMO
that include it may only be used and shared noncommercially. KAMO's own sources stay MIT.
