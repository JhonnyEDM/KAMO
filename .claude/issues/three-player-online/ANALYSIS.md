# Three-player online: analysis from three viewpoints

Companion to `ISSUE.md`. The investigation was run as three roles: a **researcher** who
proposes at least three ways to fix it and picks one, a **skeptic** whose only job is to break
that proposal, and a **mediator** who checks both sides against the code and decides what is
implemented.

## Facts everyone starts from

Reproduced on 2026-11-28 with the released 0.3.2 (`test3p.ps1`, three windows, real lobby):
all three seated and ready, the host presses Start, and every client logs
`rnet_lobby: launch refused - ... not connected over ICE`. The room shows the error.

- The server's `launch` for 3+ seats says `transport:"host"`, `relay_via:"ice"`, and still
  carries `host_endpoint` / `guest_endpoint` (excerpt in `ISSUE.md`).
- KAMO builds recomp-net without ICE (`RNET_ENABLE_ICE` off), so `rnet_host_ice_available()`
  is 0 and no waiting-room ICE agent ever connects. `ice_launch_capture()` then refuses.
- GBARecomp's runner has no ICE path anyway: `Relay`, `Pair` (2 seats), or `Hub` on the host and
  `HubGuest` on guests (`gba_netplay_transport()`), all over plain UDP.
- The `peers4` test already runs **four real `KAMO.exe` peers over `Hub`/`HubGuest`** and they
  end with identical paired archives. The in-match engine for 3-4 players is proven; only the
  lobby's choice of route is broken.

## 1. Researcher: four ways, one proposal

| # | Approach | Work | Reaches players behind strict NAT |
|---|---|---|---|
| A | **Host relay over UDP without ICE**: when the build has no ICE, ignore `relay_via:"ice"` and take the host-relay route two-seat rooms already use (host binds its advertised port and hubs, guests dial `host_endpoint`) | Small recomp-net patch | Only if the host's port is reachable (UPnP / forwarded), like two-seat rooms today |
| B | **Build with ICE** (`RNET_ENABLE_ICE=ON`, libjuice) and teach GBARecomp's runner to adopt the waiting-room agents (`rnet_lobby_ice_take_hub` / `rnet_session_start_ice_hub_adopt`, `rnet_lobby_ice_take_guest_agent` / `rnet_session_adopt_ice_agent`) | New dependency, GBARecomp patch in the runner, untested on Windows per the recomp-net handoff notes | Yes, through STUN hole punching (no TURN on this path) |
| C | **Server relay** for 3-4 (`force_input_relay`) | None in KAMO, but the lobby server is not ours and ignores the flag for 3+ (tried in 0.3.2) | Yes |
| D | **Pairs instead of a hub**: run 3-4 seats as a mesh of `Pair` links | GBARecomp has no mesh transport; every peer would need every other's endpoint, which the server does not send | No |

**Proposal: A.** It reuses a route that works today for two players and an engine path that
`peers4` already proves for four, and it is the only option that is small, ours to change, and
testable now. B is the long-term route.

## 2. Skeptic: where A breaks

1. *"The server may send no `host_endpoint` for ICE launches."* The recomp-net handoff says ICE
   launches have `host_endpoint ""` by design. If so, A has nothing to dial.
2. *"The host's port is never opened."* The waiting-room host relay (UPnP / NAT-PMP mapping and
   the guests' reachability probes) stands down when the room's caps say `relay_via:"ice"`
   (`host_relay_step()` → `room_relays_via_ice()`). If the server marks 3+ rooms that way
   during the waiting room too, patching only the launch (as `recomp-net-no-ice-fallback.patch`
   did) dials a port nobody mapped, and the server may refuse Start with `host_relay_unproven`.
3. *"The hub doubles guest-to-guest latency."* Guest B's input reaches guest C through the
   host.
4. *"Strict routers."* Without UPnP and without a forwarded port, guests cannot reach the host.
5. *"Local tests lie."* Three windows on one PC dial the host's **public** address; that needs
   NAT hairpinning, which a router may not do, so a local failure would not prove A wrong and a
   local pass needs the router to cooperate.
6. *"Silent failure."* If the hub does not link, the player just sees a "Connecting" window, and
   0.3.2's message for `ice_not_connected` promised a server relay that never happens.

## 3. Mediator: checking both sides and deciding

| Objection | Checked against | Verdict |
|---|---|---|
| 1. No endpoints | The real 3-seat launch in `ISSUE.md` carries both endpoints; this run's log line `launch transport=host ... host_endpoint=set` confirms it | Does not hold for this server |
| 2. Port never opened | `host_relay_step()` really does stand down on `relay_via_ice` | **Valid.** The fix must also keep the waiting-room host relay running, not only patch the launch |
| 3. Hub latency | Inherent to any host relay (B has it too); 3-4 seats use delay-sync | Accepted, measured in play |
| 4. Strict routers | Same requirement two-seat rooms have today | Accepted; shown to the player (below) |
| 5. Local tests | Hairpinning is a property of the router | Test locally, and still check across two networks |
| 6. Silent failure | 0.3.2's text is wrong | **Valid.** Fix the message and log the route |

**Decision: A, hardened by the skeptic's points 2 and 6.**

- `recomp/patches/recomp-net-0001-host-relay-without-ice.patch`: a build without ICE never treats
  a room as "relay over ICE", neither in the waiting room (`room_relays_via_ice()`, so the UDP
  host relay keeps the port mapped and the guests keep proving they reach it) nor at launch (the
  match takes `Hub`/`HubGuest`). Both cases print one line to stderr, and every launch prints
  its transport.
- KAMO: `netplay/online.log` records the room, the players, the connection status the host
  relay reports, the route the match takes and every error code; the room shows that
  connection status; lobby errors are explained with what to do, the code and where the log is.
- B stays the next step for players whose router cannot be reached.

## Result (2026-11-28, one PC, real lobby, three windows)

| Run | What happened |
|---|---|
| 0.3.2 release | Every client: `launch refused ... not connected over ICE`. Reproduced. |
| Fix, guests dial the public address | All three log `room asks for the host relay over ICE; ... keeping the UDP host relay`, then the launch is accepted on all three (`launch transport=host players=3 ... ice=0 ... host_endpoint=set`) and the windows open the match (`Hub` on the host, `HubGuest` on the guests). They stay on "Connecting": the guests dial this router's public address, which it does not loop back (no hairpinning), and the two guests on one PC picked the same local port. Both are artefacts of testing on one PC. |
| Fix, `KAMO_LOCAL_PEER=1` (guests dial 127.0.0.1) | **Passes, twice.** Windows titled `KAMO - Online - Player 1/2/3`, the game runs on all three, the rollback engine reports the same frame on every seat with 0 mispredictions and 17 ms RTT, and all three log `match ended (exit code 0); leaving the lobby`. |

The waiting-room log also confirmed the skeptic's point 2 (the server marks 3-seat rooms
`relay_via:"ice"` before the launch) and point 4 on this network: the host's port was found
only through STUN (`stun`, no UPnP on this router), and the guests' probes report that they
cannot reach it. The room now says exactly that, in red, to the host and to each guest.

**What is left to prove, by real players:** a three-player match across different networks.
It works when the host's UDP port is reachable (UPnP on the host's router, or the port
forwarded). A host whose router has neither cannot carry a 3-4 player match on this route;
approach B (ICE, with TURN) or C (server relay) is what removes that requirement. Two-player
rooms keep their current route: the patch only acts when the server names ICE.
