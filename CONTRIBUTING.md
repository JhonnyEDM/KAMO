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

Follow [`recomp/README.md`](recomp/README.md): it builds the recompiler, recompiles your own ROM
into the git-ignored `generated/` folder, and builds `KAMO.exe`. Keep your ROM outside the
repository, or inside `local/` (git-ignored). No BIOS is needed.

On Windows, `tools/kamo_build.ps1` builds `KAMO` and `KAMOViewProbe` and stops the build if free
memory runs low (the generated code needs about 2 GB per compile job).

## Tests

Put the path of your ROM in `local/rom.ini` (`path = ...`), then:

```powershell
.\tools\kamo_test.ps1            # all tests
.\tools\kamo_test.ps1 -Only boot,link4
```

| Test | Checks |
|---|---|
| `boot` | The game boots headless through recompiled code |
| `link2`, `link4` | 2 and 4 linked consoles reach co-op with scripted input |
| `net-delay`, `net-rollback` | Two networked peers with 40 ms latency end with identical state |
| `subgames` | Four consoles play each multiplayer sub-game |
| `peers4` | Four real `KAMO.exe` peers over loopback save identical paired archives |

All runs are windowless and muted; their output stays outside the repository. Every console
boots without a BIOS, as players run it.

## Releases

Bump the version in `project()` of `recomp/CMakeLists.txt`, build, run the tests, then
`tools/kamo_package.ps1` (needs Inno Setup 6). Attach the installer, the zip and
`SHA256SUMS.txt` to the GitHub release: installed copies update from the latest release's
`KAMO-Setup-<version>.exe`.

## Changes to GBARecomp and recomp-ui

Both are submodules. KAMO's few changes to them live as patches in `recomp/patches/`
(`gbarecomp-NNNN-*.patch`, `recomp-ui-NNNN-*.patch`) and are applied to the checkouts when CMake
configures `recomp/`.
Keep them small, and prefer proposing the change upstream.

## Pull requests

- One topic per pull request, with a short description of what changed and how you tested it.
- Run `tools/kamo_test.ps1` before sending; include the results.
- Update `docs/ROADMAP.md` when a roadmap item changes.
