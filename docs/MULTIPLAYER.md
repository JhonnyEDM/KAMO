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

**Input delay.** From the menu, the host's delay starts from the room's pings: it must cover the
slowest path an input takes, which with three or four players is guest → host → guest. The
lobby measures pings through its server, so the start errs high; recomp-net's delay controller
then lowers it within seconds and raises it when inputs arrive late. A delay fixed in
**Options > Online** turns that controller off for the match (`RBE_RB_AUTO_DELAY=0`).

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
Rooms of another KAMO version are shown but cannot be joined. Direct IP play without the
server is available from the command line for up to four players (see
[`recomp/README.md`](../recomp/README.md)).

### How the match travels

The lobby server picks the route when the host starts:

- **Two players**: the route the server names for two seats; no port forwarding is needed.
- **Three or four players**: the **host carries the match**. Its PC binds its UDP port (7777 or
  the next free one) and relays every guest's input to the others; the guests dial it. The
  server names this route "over ICE", which KAMO's build does not include, so KAMO keeps the
  host relay over plain UDP (`recomp/patches/recomp-net-0001-host-relay-without-ice.patch`).
  The host's port must be reachable: KAMO opens it with UPnP or NAT-PMP when the router allows,
  otherwise the host forwards it by hand. The room's **Connection** line shows the port, how it
  was opened and how many guests reach it; a guest's line says whether it reaches the host.

The investigation behind this is in `.claude/issues/three-player-online/` (`ISSUE.md`,
`ANALYSIS.md`), with the three-window test `test3p.ps1`.

## Diagnosing a match

- **In the game**, **F** shows the FPS and, online, a line with a quality dot (green, yellow,
  red): the input lag, the current delay, the **margin** (how many frames early the other
  players' inputs arrive; at 0 or below the match waits or rolls back) and rollback frames per
  second (`recomp/patches/gbarecomp-0004-netplay-quality.patch`).
- **`netplay/online.log`** next to `KAMO.exe` records the room (players, connection status), the
  start (delay and why), the route each PC takes, a connection summary every 30 seconds
  (fps, margin, delay, rollback, freezes), how the match ended, and every lobby error code.
  Public addresses are shortened to their last number. Over 512 KB it moves to `online.old.log`.
- Lobby errors are shown with what to do and their code, for example `ice_not_connected` or
  `host_relay_unproven` when a guest cannot reach the host.
- For testing several windows on one PC, `KAMO_LOCAL_PEER=1` makes guests dial the host on
  `127.0.0.1` (a router may not loop a PC's own public address back to it).
