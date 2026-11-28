# Issue: online rooms of 3 or 4 players never start

**Status:** fix in testing (see `ANALYSIS.md`). Rooms of three now launch over the host relay
(`recomp/patches/recomp-net-0001-host-relay-without-ice.patch`) and play on one PC; a match
across networks needs the host's UDP port to be reachable and is still to be checked by real
players. Before the fix: two-player online rooms work; rooms of three or four fail every time.
**Affects:** 0.3.0, 0.3.1 and 0.3.2. The 0.3.2 fix did not work: it was released without a
three-player test.
**Rule from the maintainer:** test three players locally (below) before releasing any fix.

## Symptom

1. The host creates a room for 3 (or 4) and everyone joins and is ready.
2. The host presses **Start match**. Nothing starts and the room shows `ice_not_connected`.
3. Every client logs it to stderr:

```
host : rnet_lobby: launch refused - "<guest>" (seat 1) is not connected over ICE
guest: rnet_lobby: launch refused - this client is not connected to the host over ICE
```

## Root cause (confirmed with debug prints of the lobby traffic)

- **The lobby server picks the route.** For rooms of three or more, its `launch` message says
  `"relay":"host","relay_via":"ice"` (host relay over ICE). It does this even when the host's
  `match_caps` ask for nothing of the kind or ask for the server relay
  (`"force_input_relay":true`). Excerpt of a real launch:
  `{"host_endpoint":"<public ip>:7777","guest_endpoint":"<public ip>:7778","match_caps":{"force_input_relay":true,...,"relay":"host","relay_via":"ice",...},"op":"launch","player_count":3,"relay_host_slot":0,"relay_via":"ice",...}`
- **KAMO's build has no ICE.** `build-native/CMakeCache.txt` has `RNET_ENABLE_ICE:BOOL=OFF`, so
  `rnet_host_ice_available()` returns 0 and no waiting-room ICE agents are ever connected.
- **recomp-net refuses the launch.** In `rnet_lobby_client.c`, the `launch` handler sets
  `transport_ice_hub = transport_host && relay_via_ice`. `ice_launch_capture()` then finds no
  COMPLETED agents, writes `ice_not_connected` and refuses the launch. This happens on every
  client, before GBARecomp is involved.
- **GBARecomp cannot run that route anyway.** `launcher_netplay_seam.h` and
  `multiplayer_runner.cpp` never take ICE agents (`rnet_lobby_ice_take_hub` /
  `rnet_session_start_ice_hub_adopt`). They start:
  - `Relay` when `force_input_relay` is set;
  - `Pair` for 2 seats;
  - otherwise `Hub` on the host (`rnet_session_start_lan_hub`) and `HubGuest` on guests.

  See `gba_netplay_transport()` in `multiplayer_launch.cpp`.
- **Why 2 players work:** for two seats the server launches without `relay_via:"ice"`. The guest
  dials `host_endpoint` (the host's public UDP port, mapped by the waiting room's host relay)
  over the `Pair` transport.

## What was tried

| Attempt | Result |
|---|---|
| 0.3.2: before `request_start`, the host sets `rnet_lobby_set_relay_via_ice(0)`, `rnet_lobby_set_relay_host_pref(0)` and `force_input_relay_set(1)` | Still `ice_not_connected`: the server adds `relay_via:"ice"` itself. Released by mistake. |
| Same, but applied before `create` | Same result; the server ignores it. |
| recomp-net patch: if `transport_ice_hub && !rnet_host_ice_available()`, clear it so the launch takes the host UDP route (`Hub`/`HubGuest`) like two seats | **Untested.** It was built, but the run was stopped before the match started. Saved as `recomp-net-no-ice-fallback.patch`. |

## State of the code

- The untested recomp-net fallback is only in `recomp-net-no-ice-fallback.patch` (not applied).
  recomp-net is a nested submodule of GBARecomp: to keep a change there, add it as a KAMO patch
  (`recomp/patches/recomp-net-*.patch`) and a `kamo_apply_patches` call for that checkout.
- The 0.3.2 server-relay forcing was removed from `recomp/src/kamo_frontend.cpp`; only a clearer
  Spanish/English message for `ice_not_connected` is left.
- 0.3.2 is released; the fix ships in 0.4.0.

## Options for the real fix

1. **Host UDP hub without ICE** (the patch above). It reuses the route two seats already take.
   - Every guest must reach the host's public UDP port. The waiting room's host relay maps it
     (UPnP); a strict router can still block it.
   - Local tests dial the host's *public* IP, so the router must support NAT hairpinning.
2. **Build with ICE** (`RNET_ENABLE_ICE=ON`, fetches libjuice) and teach GBARecomp's runner to
   adopt the agents (`rnet_lobby_ice_take_hub` / `rnet_session_start_ice_hub_adopt` on the host,
   `rnet_lobby_ice_take_guest_agent` / `rnet_session_adopt_ice_agent` on guests).
   - This is a GBARecomp patch.
   - TURN (`coturn.retcomm.net`, credentials already granted at hello) makes it work behind any
     router.
3. **Server relay** for 3–4. It needs the lobby server to honour `force_input_relay` for rooms of
   three or more. The server is not ours, so ask its maintainer.

Whichever is chosen, also consider rollback for 3–4 players: it measured smoother than delay-sync
with 4 consoles at 40 ms latency (59.5 vs 54 fps). See the notes on `--netplay-rollback`.

## How to test three players on one PC

Run `test3p.ps1` (in this folder, next to `click.ps1`, `shot.ps1` and `move.ps1`). It opens three
KAMO windows from `build-native` with a copy of a signed-in `netplay_secret`. The lobby accepts
one Discord account in several seats and names them `(2)`, `(3)`.

1. Window A creates a room of 3 (or 4); B and C join it from the list (`-Row` = the room's row).
2. A presses Start.
3. Each window's stderr goes to `%TEMP%\kamo-3p\log-a|b|c.txt` and screenshots to the same
   folder.

It uses the **real** lobby server, and other players' rooms can appear in the list: never join
them.

Pass criteria:
- the three windows are titled `KAMO - Online - Player 1/2/3`;
- the game runs on all three;
- no `launch refused` appears in the logs;
- after closing, the three `netplay/session-*.paired` files are identical.

To see the lobby traffic, temporarily add an `fprintf(stderr, ...)` of `json` at the top of the
`launch` handler in `rnet_lobby_client.c` (and in `queue_send`), then remove it.
