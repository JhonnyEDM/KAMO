# Roadmap

KAMO runs Kirby & The Amazing Mirror as a static recompilation on GBARecomp. The interpreter-based
runtime used up to 0.1.x was retired in 0.2.x.

## 1. The game on PC

- [x] Game recompiled to C++ from the player's own ROM; nothing generated is committed
- [x] No BIOS needed: BIOS calls serviced natively, in single player and online
- [x] Cold boot to gameplay through recompiled code
- [x] KAMO's own menu: ROM setup, Play alone / online, options, keyboard keys
- [x] Back to the main menu from the game
- [ ] Game menu (Esc) translated; today only KAMO's menu is in Spanish and English
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
- [x] Continue a match from the room
- [ ] Write online progress back to each player's single-player save
- [ ] LAN / direct-IP rooms with more than two seats

## 3. Release

- [x] Windows build: installer and zip on GitHub Releases
- [x] Automatic updates (installed copies)
- [x] RecompLauncher support (`recomplauncher.json` in the zip)
- [ ] macOS and Linux builds
- [ ] 1.0
