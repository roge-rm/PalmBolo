// Debug only (LOOPBACK_TEST): a second copy of the game in the app, at the
// other end of the loopback link, so network play can be tried on one
// Palm. It joins when this Palm hosts and hosts when this Palm joins, and
// its player drives by a simple script. Segment net.
#ifndef LOOPTEST_H
#define LOOPTEST_H

#include "game/game.h"
#include "net/lockstep.h"

// Starts the loopback afresh and opens the second copy's end as role.
Boolean SEC_NET LoopPeerOpen(UInt8 role);
// Takes and sends the second copy's frames, sets up its game when the
// host starts and runs every tick it can. slot is this Palm's tank, to
// put the viewer back.
void SEC_NET LoopPeerPoll(UInt32 now, UInt8 slot);
void SEC_NET LoopPeerClose(void);
// Saves both states after a desync to the database PalmBoloDesync.
void SEC_NET LoopSaveDesync(const GameState *g, const Lockstep *ls);

#endif
