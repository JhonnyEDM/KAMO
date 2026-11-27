# Roadmap

## 1. The game on PC ✅

- [x] ROM verification for the USA release
- [x] Graphics, sound, controllers, battery saves
- [x] Deterministic save states
- [ ] Full playthrough without issues
- [ ] Map the game's RAM to re-enable cheats, rumble events and widescreen

## 2. Local multiplayer 🔨

- [x] Several consoles in one process
- [x] Serial multiplayer mode and the virtual link cable
- [x] The game's lobby detects 2–4 players and starts co-op
- [x] Four consoles stay in sync; identical input gives identical frames
- [ ] Play with 2–4 controllers on one PC
- [ ] Accurate instruction timing (linked consoles currently run the CPU at 200 %)
- [ ] Long sessions: bosses, area changes, multiplayer sub-games, each player's save

## 3. Online co-op

- [x] Input exchange over UDP (ENet), lockstep with input delay
- [x] Direct connection by IP address (`KAMO-Netplay` test build)
- [x] Version and ROM check on join; desync detection by state hashes
- [x] Automatic input delay from the measured ping
- [x] Frame pacing: a PC that runs ahead slows down slightly instead of stalling
- [x] In-window menu to host or join, and self-update from GitHub releases
- [x] Session recordings (`kamo_link --replay`) to reproduce problems exactly
- [ ] Session start with each player's own save
- [ ] Resynchronisation after a desync
- [ ] Lobby service: public and private sessions, codes, ready / start / kick
- [ ] NAT traversal and relay
- [ ] Rollback

## 4. Static recompilation 🧪

Built on GBARecomp (`recomp/`, see its README).

- [x] Game and BIOS recompiled to C++ from the player's own files; nothing generated is committed
- [x] Cold boot to gameplay with no interpreter fallback in the tested route
- [x] Four linked consoles in one process reach the hub with no link errors
- [x] Online through GBARecomp netplay: four peers (direct IP, delay-sync) end with identical state
- [x] Code missed by the recompiler is logged and listed in `recomp/game.toml`
- [ ] Coverage of the whole game, including bosses and multiplayer sub-games
- [x] Launcher window and online lobby (recomp-ui): create or join public and private rooms
- [ ] Four-player room tested between real PCs over the internet
- [ ] LAN / direct-IP rooms with more than two seats (the lobby server has four)
- [ ] Each player's own save in online sessions
- [ ] Release builds of the recompiled version

## 5. Release

- [ ] Builds for Windows, macOS and Linux
- [ ] RecompLauncher support (`recomplauncher.json`)
- [ ] 1.0
