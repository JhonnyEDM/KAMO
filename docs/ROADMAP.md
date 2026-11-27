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
- [ ] Accurate instruction timing (linked consoles currently run the CPU at 125 %)
- [ ] Long sessions: bosses, area changes, multiplayer sub-games, each player's save

## 3. Online co-op

- [x] Input exchange over UDP (ENet), lockstep with input delay
- [x] Direct connection by IP address (`KAMO-Netplay` test build)
- [x] Version and ROM check on join; desync detection by state hashes
- [ ] Automatic input delay from ping
- [ ] Session start with each player's own save
- [ ] Resynchronisation after a desync
- [ ] Lobby service: public and private sessions, codes, ready / start / kick
- [ ] NAT traversal and relay
- [ ] Rollback

## 4. Static recompilation

- [ ] Function tracer
- [ ] ARM/Thumb to C recompiler
- [ ] Runtime integration with interpreter fallback
- [ ] Verification mode: recompiled code against the interpreter

## 5. Release

- [ ] Builds for Windows, macOS and Linux
- [ ] RecompLauncher support (`recomplauncher.json`)
- [ ] 1.0
