# Multiplayer

Kirby & The Amazing Mirror supports up to four players over the Game Boy Advance link cable, with
one cartridge per player. KAMO runs that mode unchanged and replaces the cable with the internet.
Online play is GBARecomp's whole-session netplay; this page explains it for KAMO.

## The link cable

The game uses the serial port in **multiplayer mode**. Player 1 is the parent; players 2–4 are
children. Many times per frame the parent starts a transfer: every console sends 16 bits and
receives the values of all four. The game checks that every console stays in step and shows
*Communication Error!* when one falls behind.

## Why the cable never goes over the network

Emulators that offer link play usually forward each serial transfer to the other players. Each
transfer must be answered almost immediately, many times per frame, so even a short delay over
the internet stalls the game or breaks its synchronisation.

KAMO uses **input-based netcode** instead:

```
        Player 1's PC                              Player 2's PC
┌──────────────────────────────┐          ┌──────────────────────────────┐
│ Console 1 ─┐                 │          │ Console 1 ─┐                 │
│ Console 2 ─┼─ virtual cable  │          │ Console 2 ─┼─ virtual cable  │
│ Shows console 1              │          │ Shows console 2              │
└──────────────┬───────────────┘          └──────────────┬───────────────┘
               │      button presses, once per frame     │
               └──────────────── internet ───────────────┘
```

- Every PC runs **all the consoles** of the match, joined by an in-process cable.
- Only **button presses** travel over the network: a few bytes per player per frame.
- The recompiled game and the runtime are deterministic, so identical input gives identical
  consoles on every PC. Each player sees and hears only their own console.
- Netplay refuses the interpreter fallback: every piece of game code a match reaches must have
  been recompiled. Code the recompiler missed is added to `recomp/game.toml`.

### Rollback and delay-sync

Two players use **rollback** by default: local input applies immediately, remote input is
predicted, and when the prediction is wrong the consoles are restored and re-run. Three or four
players use **delay-sync**: input applies a few frames after it is pressed (`--netplay-delay`,
default 6). Both modes work at every size.

### Starting and saving a match

The ROM never travels over the network. Each player uses their own; the match checks the ROM and
the KAMO build of every player before the first frame and refuses mismatches. No BIOS is used:
every console boots straight into the cartridge and its BIOS calls are serviced natively, the
same way on every PC.

Closing the game window asks every player to save together. The PCs agree on one frame and each
writes the same **paired archive** (`netplay/session-<ID>.paired`) holding every console. A new
match is resumed from that archive when every player ticks **Continue the last saved match** in
the room (or with `--continue-online` / `--netplay-resume <file>`); every player must resume the
same match with the same KAMO version.
Online progress is not written to the players' single-player saves.

## Rooms

KAMO's **Play online** screen connects, through recomp-ui's lobby backend, to the public lobby
server `netplay.retcomm.net`:

| | Public | Private |
|---|---|---|
| Listed in the room browser | ✅ | ❌ |
| Password | — | ✅ |
| Players | 2–4 | 2–4 |

Inside a room, players can chat; the host is marked with a crown.
Rooms of another KAMO version are shown but cannot be joined. During the match, input is relayed
through the server's UDP relay, so no port forwarding is needed. Direct IP play without the
server is available from the command line for up to four players (see
[`recomp/README.md`](../recomp/README.md)).
