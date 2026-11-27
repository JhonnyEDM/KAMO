// Connects GBARecomp's linked-session probe (KAMOViewProbe) to this game's netplay description.
#include "kamo_game.h"

gbarecomp::GbaNetplayLaunch gba_view_probe_game() { return *kamo::make_netplay_launch(); }
