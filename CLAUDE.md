# KAMO — notes for Claude Code

Native PC version of Kirby & The Amazing Mirror (USA) with online co-op for up to four players.
C++20, CMake, SDL3, Dear ImGui. Start with `README.md`, `docs/ARCHITECTURE.md`,
`docs/MULTIPLAYER.md` and `docs/ROADMAP.md`; setup and testing are in `CONTRIBUTING.md`.

## Hard rules

- **No game content in the repository, ever:** no ROMs, BIOS files, saves, save states,
  extracted assets, screenshots of the game, or code generated from the ROM. `generated/`,
  `local/` and `build/` are git-ignored; check `git status` before every commit.
- **Ask the maintainer before copying code from another project**, even permissively licensed
  code. Credit approved reuse in `THIRD_PARTY_NOTICES.md` and the README credits.
- Code, comments, commit messages and docs are in English. Keep public docs short and about KAMO.

## Design constraints

- **Several consoles per process.** `gba::Gba` holds all of a console's state; no globals or
  statics with per-console data in the runtime.
- **Deterministic emulation.** No wall-clock time, threads or host randomness inside a console.
  The same input must give the same frames (`kamo_state_test`, `kamo_link`).
- **Online play is input-based netcode.** Every PC runs all consoles joined by the in-process
  link cable (`src/gba/link.h`); only button input crosses the network. Never send serial
  transfers over the network.
- **The online layer talks only to the link cable**, never to CPU internals, so the interpreter can
  later be replaced by recompiled code without touching it.
- **Game-specific RAM features** (cheats, rumble events, widescreen, save header check) still use
  addresses from KNID's game and stay off until `game::kRamMapVerified` is true. Don't enable them
  without mapping this game's RAM.
- Linked consoles run the CPU at 200 % (`LinkCable::kLinkCpuSpeedPct`) to make up for
  instruction timing that is slower than hardware; see the roadmap.

## Workflow

- Build: `.\build.ps1` on Windows, or the CMake commands in the README. Run `ctest --test-dir build`
  before committing; set `KAMO_ROM` to a verified ROM to include the determinism test.
- Multiplayer changes: verify with `kamo_link` (see `CONTRIBUTING.md`), including a
  four-console run and a determinism check (two runs, identical frame hashes).
- Keep `docs/ROADMAP.md` up to date.
- `CLAUDE.local.md` (git-ignored) holds personal preferences of whoever is working locally.
