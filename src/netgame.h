// Network play on the Palm: the links this Palm has, the lobby, sending
// and getting the map, and the turns of a game. Segment net, round the
// lockstep code in src/net.
#ifndef NETGAME_H
#define NETGAME_H

#include "game/game.h"
#include "net/link.h"

// A clock in ms for the lockstep code.
UInt32 SEC_NET NetNowMs(void);

// Links this Palm can use, in the order shown. The loopback is only in
// LOOPBACK_TEST builds; experimental IrLib is in DEBUG builds.
#define kNetMaxLinks    4
UInt8 SEC_NET NetLinks(UInt8 *kinds, const Char **names);

// Where a network game is.
enum {
	NET_OFF,
	NET_LOBBY,      // host: taking players; joiner: looking for a game or in its lobby
	NET_STARTING,   // the game is being set up: map sent or loaded, waiting for the others
	NET_PLAY,       // the game is set up in g: go to the game
	NET_STOPPED     // see NetWhy
};

// Opens a lobby on link kind to host a game on map index (named name)
// with the prefs' settings, or looks for a game to join. The game goes in
// g. False with the reason in NetWhy if it can't.
Boolean SEC_NET NetHost(UInt8 kind, UInt16 index, const Char *name, struct GameState *g);
Boolean SEC_NET NetJoin(UInt8 kind, struct GameState *g);
// Host: starts the game with the players seated.
Boolean SEC_NET NetStart(void);
// Leaves the lobby or the game and closes the link.
void SEC_NET NetEnd(void);
// The game is over on this Palm: waits a moment for the others to reach
// the end too, then NetEnd.
void SEC_NET NetFinish(const struct GameState *g);

// Takes what has come and sends what's due; sets up a joiner's game when
// the host starts it. Returns the phase.
UInt8 SEC_NET NetPoll(void);
// Services IR during long Palm operations without dispatching UI events.
void SEC_NET NetService(void);
Boolean SEC_NET NetServiceActive(void);
UInt8 SEC_NET NetPhase(void);
Boolean SEC_NET NetIsHost(void);
// This Palm's slot (its tank) and why the game stopped.
UInt8 SEC_NET NetSlot(void);
const Char * SEC_NET NetWhy(void);

// The lobby, a row for each tank: players first, then bots. Fills name
// (kNameLen bytes) and a note: the team, or how much of the map they have.
// Returns the rows.
UInt8 SEC_NET NetRows(void);
void SEC_NET NetRow(UInt8 i, Char *name, Char *note);
// The map's name and a line on how things stand.
const Char * SEC_NET NetMapName(void);
void SEC_NET NetStatus(Char *buf);
// A count that changes when the lobby does, to know when to redraw.
UInt16 SEC_NET NetLobbySeq(void);
// Whether there are joiners to start with.
Boolean SEC_NET NetCanStart(void);
// A player's name in the game, for the results.
const Char * SEC_NET NetName(UInt8 slot);

// In the game: whether the next tick can run, and runs it with this
// player's keys.
Boolean SEC_NET NetReady(void);
void SEC_NET NetTick(struct GameState *g, const GameInput *in);
// The line to show over the game (who it waits for, who a bot took over),
// or false for none. line is at least 40 bytes.
Boolean SEC_NET NetLine(const struct GameState *g, Char *line);

#endif
