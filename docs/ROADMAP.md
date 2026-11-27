# Roadmap

KAMO runs Kirby & The Amazing Mirror as a static recompilation on GBARecomp. The interpreter-based
runtime used up to 0.1.x was retired in 0.2.x.

## 1. The game on PC

- [x] Game and BIOS recompiled to C++ from the player's own files; nothing generated is committed
- [x] Cold boot to gameplay through recompiled code
- [x] Launcher window (recomp-ui): ROM and BIOS check, settings, controls
- [x] The three multiplayer sub-games with four consoles (test `subgames`)
- [ ] Coverage of the whole game, including bosses
- [ ] Full single-player playthrough without issues

## 2. Online co-op

- [x] Whole-session netplay: every PC runs all consoles, only input crosses the network
- [x] Rollback for two players, delay-sync for three or four
- [x] Online lobby: public and private rooms, up to four players, UDP relay
- [x] Two players between real PCs over the internet
- [x] Shared match saves, and continuing the newest one (`--continue-online`)
- [ ] Four players between real PCs
- [ ] Continue a match from the launcher, without a separate shortcut
- [ ] Write online progress back to each player's single-player save
- [ ] LAN / direct-IP rooms with more than two seats

## 3. Release

- [x] Windows build (zip on GitHub Releases)
- [ ] Automatic updates
- [ ] macOS and Linux builds
- [ ] 1.0
