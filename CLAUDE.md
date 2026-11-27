# KAMO — notes for Claude Code

Native PC version of Kirby & The Amazing Mirror (USA) with online co-op for up to four players.
Static recompilation on GBARecomp, C++20, CMake, MSYS2 MinGW. Start with `README.md`,
`docs/MULTIPLAYER.md` and `docs/ROADMAP.md`; setup and testing are in `CONTRIBUTING.md`.

## Hard rules

- **No game content in the repository, ever:** no ROMs, BIOS files, saves, save states,
  extracted assets, screenshots of the game, or code generated from the ROM. `generated/`,
  `local/` and `build/` are git-ignored; check `git status` before every commit.
- **Ask the maintainer before copying code from another project**, even permissively licensed
  code. Credit approved reuse in `THIRD_PARTY_NOTICES.md` and the README credits.
- Code, comments, commit messages and docs are in English. Keep public docs short and about KAMO.

## How KAMO is built

- The game is a static recompilation on GBARecomp (submodule `external/gbarecomp`, PolyForm
  Noncommercial); the launcher and lobby are recomp-ui (`external/recomp-ui`, MIT). KAMO's own
  code is `recomp/` (host, `game.toml`, patches, package files) and `tools/`.
- Generated game and BIOS code lives in the git-ignored `generated/`; see `recomp/README.md`.
- KAMO's changes to GBARecomp are patches in `recomp/patches/`, applied at CMake configure time.
  Never commit inside the submodule.
- Netplay is GBARecomp's whole-session netplay: every peer runs all linked consoles, only input
  crosses the network. It refuses the interpreter fallback, so add code the recompiler missed to
  `recomp/game.toml` (`[[extra_func]]`, `[[code_copy]]`) and regenerate.
- The generated code is large: compile with 64 codegen shards and at most `-j6`
  (`tools/kamo_build.ps1` has a RAM watchdog).

## Workflow

- Build: `tools/kamo_build.ps1`. Test: `tools/kamo_test.ps1` (ROM and BIOS paths in
  `local/rom.ini` and `local/bios.ini`); every test must pass before committing.
- Multiplayer changes: also check a real two-window match when possible.
- Keep `docs/ROADMAP.md` up to date.
- `CLAUDE.local.md` (git-ignored) holds personal preferences of whoever is working locally.
