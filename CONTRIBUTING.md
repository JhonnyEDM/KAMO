# Contributing to KAMO

Thanks for helping! This page covers setup, testing and the rules every change follows.

## Ground rules

- **No game content in the repository:** no ROMs, BIOS files, saves, save states, extracted
  assets, screenshots of the game, or code generated from the ROM. Pull requests that include any
  will be closed.
- Don't copy code from other projects without asking first in an issue. Approved reuse is
  credited in `THIRD_PARTY_NOTICES.md`.
- Code, comments, commit messages and docs are in English.

## Setup

1. Install CMake 3.24+, a C++20 compiler and Ninja.
   On Windows: Visual Studio 2022 or its Build Tools with the *Desktop development with C++*
   workload.
2. Get your own dump of Kirby & The Amazing Mirror (USA). It must match SHA-1
   `274b102b6d940f46861a92b4e65f89a51815c12c`. Keep it **outside** the repository, or inside
   `local/` (git-ignored).
3. Build:

   ```powershell
   .\build.ps1          # Windows (Release, build\)
   .\build.ps1 -Debug   # Windows (Debug, build-debug\)
   ```

   ```sh
   cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build
   ```

4. Run `build/KAMO` and choose your ROM.

## Tests

```sh
ctest --test-dir build --output-on-failure
```

Tests that need the game skip themselves without a ROM. To include them, point `KAMO_ROM` at your
dump:

```powershell
$env:KAMO_ROM = "C:\path\to\your\rom.gba"; ctest --test-dir build --output-on-failure
```

## Running the game without a window

```sh
build/KAMO --headless --game <rom> --frames 900 --dump-frame title.bmp
build/KAMO --headless --game <rom> --frames 1200 --input 900:8,906:0 --dump-frame menu.bmp
```

`--input` takes `frame:keys` pairs, with keys as a hex mask: A `1`, B `2`, Select `4`, Start `8`,
Right `10`, Left `20`, Up `40`, Down `80`, R `100`, L `200`.

## Multiplayer: `kamo_link`

`kamo_link` runs 2–4 consoles joined by the virtual link cable, with scripted input per console:

```sh
build/kamo_link --rom <rom> --units 4 --frames 3000 --out out/ \
  --input 0:900:8,906:0,1000:1,1006:0 --input 1:900:8,906:0 ... --dump-at 1500,2400
```

It prints the transfer count every second and writes `out/unitN.bmp` (and `unitN-fFRAME.bmp` for
`--dump-at`). Useful options: `--trace-from <frame>` logs serial register writes;
`--cpu-pct <n>` overrides the linked CPU speed.

From boot, this input reaches the game's multiplayer screen on every console:
`900:8,906:0,1000:1,1006:0,1060:1,1066:0,1130:80,1136:0,1190:1,1196:0`
(Start, A, A, Down, A). Player 1 then confirms with A.

Before sending a multiplayer change, check that:

- four consoles reach co-op and keep transferring with no *Communication Error!*;
- two runs with the same input produce identical `unitN.bmp` files.

Keep `out/` outside the repository; its images come from the game.

## Pull requests

- One topic per pull request, with a short description of what changed and how you tested it.
- Update `docs/ROADMAP.md` when a roadmap item changes.

## Online: `KAMO-Netplay`

Two copies on one PC, connected over the loopback interface (no firewall prompt):

```sh
build/KAMO-Netplay --host --loopback --players 2 --rom <rom> --headless --frames 3600 --input <script>
build/KAMO-Netplay --join 127.0.0.1 --rom <rom> --headless --frames 3600 --input <script>
```

Both print `final frame N hash H` at the end; the hashes must match, and no `DESYNC` line may
appear. Without `--headless` each copy opens a window and reads the keyboard and the first
controller.
