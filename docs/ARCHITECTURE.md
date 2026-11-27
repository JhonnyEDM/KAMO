# Architecture

```
┌──────────────────────────────────────────────────────────────────┐
│ Frontend                                                         │
│ Launcher · settings menu · input · saves · updates               │
├──────────────────────────────────────────────────────────────────┤
│ Online                                                           │
│ Sessions · input exchange · lockstep / rollback                  │
├──────────────────────────────────────────────────────────────────┤
│ GBA runtime                                                      │
│ Memory · graphics · sound · DMA · timers · interrupts            │
│ BIOS calls (no BIOS needed) · battery save · link cable          │
├──────────────────────────────────────────────────────────────────┤
│ Game code                                                        │
│ Interpreter today · statically recompiled native code later      │
└──────────────────────────────────────────────────────────────────┘
                SDL3 · Dear ImGui · CMake · C++20
```

## GBA runtime (`src/gba`)

- **`Gba`** is one complete console. It holds no global state, so several consoles can run in the
  same process — the basis of both local and online multiplayer.
- **CPU:** an ARM7TDMI interpreter (ARM and Thumb).
- **Graphics:** scanline renderer for tile and bitmap modes, sprites, blending and windows.
- **Sound:** the four PSG channels and the two DMA sound FIFOs.
- **BIOS:** the system calls the game uses are implemented directly; no BIOS file is needed.
- **Save states:** a console can be saved and restored between frames. Running from a saved state
  with the same input gives the same result, which is what rollback and resynchronisation need.
- **Link cable (`link.h`):** serial multiplayer mode between 2–4 consoles in one process. See
  [MULTIPLAYER.md](MULTIPLAYER.md).

## Frontend (`src/app`, `src/options`, `src/update`)

The launcher (ROM selection, Play / Continue), the settings menu, keyboard and controller input
with remapping and rumble, save import and export, save state slots with thumbnails, rewind, and
automatic updates from GitHub Releases.

## The ROM

On first launch the player picks their own dump. KAMO cleans up common dump variations in memory
(copier headers, overdumps, trimmed files, rewritten headers) and accepts the file only if it then
matches the USA release exactly (SHA-1 `274b102b6d940f46861a92b4e65f89a51815c12c`). Only its path is
stored. Graphics, music and levels are read from it at run time; nothing from the game is part of
KAMO.

## Static recompilation (planned)

The interpreter gets the whole game running today. The long-term plan is to translate the game's
ARM and Thumb code into C ahead of time and compile it into KAMO:

1. **Trace** (`tools/kamo_disasm`): follow the code from the entry point and find every function;
   indirect jumps that can't be proven are listed rather than guessed.
2. **Recompile:** turn each function into C that calls the runtime for memory and hardware access.
3. **Run:** the runtime executes recompiled functions natively and falls back to the interpreter for
   anything not covered.

The output is generated from each developer's own ROM into `generated/`, which is never committed.
The online layer only talks to the link cable, so it is unaffected by this change.

## Determinism

Online play depends on every PC computing exactly the same frames:

- Emulation advances by frames and cycles only — never by wall-clock time.
- The game's random numbers come from its own state.
- Sessions check that everyone runs the same KAMO version and ROM.
- `kamo_state_test` and `kamo_link` verify that identical input gives identical output.

## Developer tools

| Tool | Purpose |
|---|---|
| `kamo_link` | Runs 2–4 linked consoles without a window, with scripted input, frame dumps and a serial-port trace |
| `kamo_disasm` | Traces the ROM's code (needs Capstone) |
| `kamo_icon` | Renders the app icon and logo |
| `KAMO --headless` | Runs the game without a window for automated checks |
