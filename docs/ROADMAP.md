# Roadmap

KAMO runs Kirby & The Amazing Mirror as a static recompilation on GBARecomp. The interpreter-based
runtime used up to 0.1.x was retired in 0.2.x.

## 1. The game on PC

- [x] Game recompiled to C++ from the player's own ROM; nothing generated is committed
- [x] No BIOS needed: BIOS calls serviced natively, in single player and online
- [x] Cold boot to gameplay through recompiled code
- [x] KAMO's own menu: ROM setup, Play alone / online, options, keyboard keys
- [x] Back to the main menu from the game
- [x] Pixel-art main menu, and the same look on every menu screen (0.4.0)
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
- [ ] Four players between real PCs (rooms of 3-4 now launch over the host relay and play on
  one PC; across networks the host's UDP port must be reachable: UPnP or a forwarded port)
- [x] Continue a match from the room
- [x] Room chat, host crown, and a connection line in the room (0.4.0)
- [x] Connection readout in the game (F) and an online log with 30 s summaries (0.4.0)
- [x] Starting input delay sized for the host relay; a fixed delay is respected (0.4.0)
- [x] Each player's Kirby colour, seen by everyone, and names above the Kirbys
- [ ] The same colour for your Kirby when playing alone
- [ ] Host relay over ICE (no port to open on the host), or the server relay, for 3-4 players
- [ ] Write online progress back to each player's single-player save
- [ ] LAN / direct-IP rooms with more than two seats

## 3. Release

- [x] Windows build: installer and zip on GitHub Releases
- [x] Automatic updates, checked and verified each time the menu opens: installed copies run the installer, portable copies swap their own files
- [x] RecompLauncher support (`recomplauncher.json` in the zip)
- [ ] macOS and Linux builds
- [ ] 1.0
