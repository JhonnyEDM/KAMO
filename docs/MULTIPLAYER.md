# Multiplayer

Kirby & The Amazing Mirror supports up to four players over the Game Boy Advance link cable, with
one cartridge per player. KAMO runs that mode unchanged and replaces the cable.

## The link cable

The game uses the serial port in **multiplayer mode**. Player 1 is the parent; players 2–4 are
children. Around sixteen times per frame the parent starts a transfer: every console sends 16 bits
and receives the values of all four. The game exchanges input and synchronisation data this way
and checks that every console stays in step, showing *Communication Error!* when one falls behind.

KAMO emulates the serial port (`src/gba/link.h`). A `LinkCable` joins 2–4 consoles running in the
same process and interleaves them in short slices, so their clocks never drift apart. The game's own
lobby detects the cable and starts co-op exactly as it would on hardware.

## Why the cable never goes over the network

Emulators that offer link play usually forward each serial transfer to the other players. Each
transfer must be answered almost immediately, sixteen times per frame, so even a short delay over
the internet stalls the game or breaks its synchronisation.

KAMO uses **input-based netcode** instead:

```
        Player 1's PC                              Player 2's PC
┌──────────────────────────────┐          ┌──────────────────────────────┐
│ Console 1 ─┐                 │          │ Console 1 ─┐                 │
│ Console 2 ─┼─ virtual cable  │          │ Console 2 ─┼─ virtual cable  │
│ Console 3 ─┤                 │          │ Console 3 ─┤                 │
│ Console 4 ─┘                 │          │ Console 4 ─┘                 │
│ Shows console 1              │          │ Shows console 2              │
└──────────────┬───────────────┘          └──────────────┬───────────────┘
               │      button presses, once per frame     │
               └──────────────── internet ───────────────┘
```

- Every PC runs **all the consoles** of the session, joined by the virtual cable.
- Only **button presses** travel over the network: a few bytes per player per frame.
- The runtime is deterministic, so identical input produces identical consoles on every PC. Each
  player sees and hears only their own console.
- Running four consoles is light work for a modern PC; only the local one is drawn and played.

### Input delay and rollback

The first version uses **lockstep with input delay**: input is applied a few frames after it is
pressed (chosen automatically from the connection's ping), and a PC waits briefly if another
player's input for a frame hasn't arrived.

A later version adds **rollback**: local input is applied immediately, remote input is predicted,
and when the real input differs the consoles are restored to an earlier state and re-run. A GBA's
state is small, so this is fast.

### Starting a session

All copies must start identically. When the host starts the game, every player sends their own save
file to the others, every PC boots the consoles with those saves, and all of them enter the game's
multiplayer mode on the same frame. At the end, each player keeps only their own console's save.

Every few seconds the PCs compare a checksum of the console state. If they ever differ, the host's
state is sent to everyone to resynchronise.

The ROM never travels over the network: each player uses their own, and the session checks that
everyone has the same version.

## Sessions

| | Public | Private |
|---|---|---|
| Listed in **Browse sessions** | ✅ | ❌ |
| Join with a 6-character code | ✅ | ✅ (the only way) |
| Optional password | — | ✅ |
| Players | 2–4 | 2–4 |

In the lobby each player picks a save file and marks themselves ready; the host starts the game.
Everyone must run the same KAMO version with the same ROM.

A small lobby service lists sessions, hands out codes and helps players connect directly. When a
direct connection isn't possible, traffic goes through a relay (well under 1 KB/s per player).
Players can also connect directly by IP address.

Cheats, save states and fast forward are disabled in online play.
