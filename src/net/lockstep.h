// Lockstep network play. Every device runs the whole game and only the
// players' inputs travel: each turn of LS_TICKS ticks steps once every
// player's input for it is in. The host gathers the inputs and sends each
// turn, all players' inputs together, to everyone, so every device steps
// the same turns. doc/lockstep.md has the messages and byte layouts.
//
// Plain C with no Palm headers and no globals: the host test runs several
// of these in one process. Times are in ms from any clock that only goes
// up.

#ifndef LOCKSTEP_H
#define LOCKSTEP_H

#include <stdint.h>
#include "../game/game.h"
#include "link.h"

#define LS_VERSION      2
#define LS_TICKS        4       // ticks in a turn, 80 ms
#define LS_SLOTS        GAME_MAX_TANKS  // player n drives tank n
#define LS_WIN          32      // turns kept, a power of two
#define LS_MAX_DELAY    7       // input delay can't pass this, see LS_WIN in lockstep.c
#define LS_NAME         16      // name bytes, with the 0 at the end
#define LS_SUM_TURNS    64      // turns between checksums, 5.12 s
#define LS_DROP_MS      3000    // a player silent this long goes to a bot
#define LS_WAIT_MS      300     // a stall shorter than this isn't shown
#define LS_START_MS     10000   // silence allowed while a game starts (map load, map transfer)
#define LS_MAPNAME      32      // map name bytes, with the 0
#define LS_CHUNK        61      // map bytes in a MAPDATA frame
#define LS_MAP_WIN      8       // map chunks sent ahead of what the joiner has
#define LS_END_MS       5000    // longest wait at the end for the others to have every turn

// The map file's bytes for a transfer. Host: copies n bytes from off into
// dst. Joiner: takes n bytes for off, which come in order; returns 0, or
// non-zero if it can't keep them.
typedef void (*LsMapGet)(void *ctx, uint16_t off, uint8_t *dst, uint8_t n);
typedef uint8_t (*LsMapPut)(void *ctx, uint16_t off, const uint8_t *src, uint8_t n);

// One player's input for a turn: the keys of each tick, the options and a
// builder order, which is given on the turn's first tick.
typedef struct LsInput {
	uint8_t keys[LS_TICKS];
	uint8_t opts, build, buildX, buildY;
} LsInput;

// What the host sends everyone before the game: all the game needs to
// start the same everywhere.
typedef struct LsStart {
	uint32_t seed;
	uint32_t mapSum;        // CRC-32 of the .map file's bytes
	uint16_t mapLen;        // its length
	uint8_t gameType, hiddenMines, timeLimit;   // as in GameSetup
	uint8_t numPlayers;     // players in slots 0 up; the host is 0
	uint8_t numBots, botLevel;      // bots after the players; botLevel also for handed over tanks
	uint8_t delay;          // input delay in turns
	uint8_t sumTurns;       // turns between checksums
	uint8_t dropSecs;       // seconds of silence before a player goes to a bot
	uint8_t allies[LS_SLOTS];       // bit n set: tank's player allied to player n
	char names[LS_SLOTS][LS_NAME];  // the players' names
} LsStart;

// State of a Lockstep.
enum {
	LS_IDLE,
	LS_LOBBY,       // host: taking joiners' HELLOs
	LS_HELLO,       // joiner: asking to join
	LS_STARTING,    // host: START sent, waiting for READYs; joiner: START is in, call ls_join_ready
	LS_PLAYING,
	LS_STOPPED      // see stop
};

// Why a game stopped.
enum {
	LS_STOP_NONE,
	LS_STOP_DESYNC,     // checksums differ
	LS_STOP_HOST_LEFT,  // the host left the game
	LS_STOP_HOST_LOST,  // nothing heard from the host for the drop time
	LS_STOP_VERSION,    // the host runs another protocol version
	LS_STOP_FULL,       // no seat left, or the game has started
	LS_STOP_MAP,        // the map didn't arrive whole, or couldn't be kept
	LS_STOP_DROPPED,    // the host gave our tank to a bot
	LS_STOP_END         // the host's game is over (sent, never a stop reason)
};

// What fetch says on a joiner.
enum { LS_MAP_NONE, LS_MAP_GETTING, LS_MAP_GOT };

typedef struct Lockstep {
	Link link;
	LsStart start;
	uint32_t now;           // ms of the last ls_poll
	uint32_t turn;          // the turn being run
	uint32_t bundled;       // host: the first turn not yet put together
	uint8_t sub;            // tick of the turn to run next
	uint8_t host, slot, state;
	uint8_t humans;         // slots still played by someone, as of the last turn put together
	uint8_t seated;         // host: slots taken in the lobby (0 is the host)
	uint8_t ready;          // host: slots that sent READY; joiner: names in
	uint8_t node[LS_SLOTS]; // host: each slot's link node
	uint8_t leaving;        // host: slots that sent LEAVE
	uint8_t gotStart;       // joiner: START came
	char me[LS_NAME];       // this player's name

	// The lobby, as the host shows it to joiners: map name, bots, teams.
	// A joiner learns its slot and the seated slots from SEATS.
	char mapName[LS_MAPNAME];
	uint8_t lobbyBots, lobbyTeams;
	uint8_t inLobby;        // joiner: SEATS came, so the host has seated us
	uint8_t setupNext[LS_SLOTS]; // next frame in a partial roster or START batch
	uint8_t rosterNew;      // host: slots to send the roster to now
	uint32_t rosterAt;      // host: when the roster last went to everyone

	// Sending the map. Host: each slot's next byte wanted and next byte
	// sent. Joiner: bytes in so far, their CRC, and chunks that came early.
	LsMapGet mapGet;
	LsMapPut mapPut;
	void *mapCtx;
	uint8_t fetch;          // joiner: LS_MAP_*
	uint8_t fetching;       // host: slots asking for the map
	uint16_t mapAck[LS_SLOTS], mapSent[LS_SLOTS];
	uint32_t mapAckAt[LS_SLOTS], mapFast[LS_SLOTS];
	uint16_t mapHave, mapAsked;
	uint8_t mapNack;        // joiner: a chunk came early, so one before it is missing
	uint32_t mapCrc, mapAskAt;
	uint8_t mapEarly;       // chunks past mapHave that are in, bit (chunk number % LS_MAP_WIN)
	uint8_t mapBuf[LS_MAP_WIN][LS_CHUNK];

	// The turns, a ring of LS_WIN by turn number.
	uint32_t ringTurn[LS_WIN];
	uint8_t have[LS_WIN];   // slots whose input is in
	uint8_t done[LS_WIN];   // the turn is complete: put together (host) or come (joiner)
	uint8_t act[LS_WIN];    // slots whose inputs the turn holds
	uint8_t handover[LS_WIN];   // slots handed to bots as the turn starts
	LsInput in[LS_WIN][LS_SLOTS];
	LsInput mine;           // own input being made, for turn + delay

	// What the other side has. Host: per slot. Joiner: entry 0, the host.
	uint32_t ackNext[LS_SLOTS];     // next turn they need from us
	uint32_t sentNext[LS_SLOTS];    // next turn we haven't sent them
	uint32_t ackTime[LS_SLOTS];     // when ackNext last moved
	uint32_t heard[LS_SLOTS];       // when we last heard from them
	uint32_t lastSend[LS_SLOTS];
	uint32_t pingAt[LS_SLOTS];
	uint16_t rtt[LS_SLOTS];         // round trip, smoothed, ms; 0 until measured
	uint8_t sumLeft[LS_SLOTS];      // frames still to carry our latest checksum
	uint8_t stopPending;            // peers still owed the current STOP round
	uint8_t stopLeft;               // STOP frames still to send

	// Checksums: our last few, and one from each other side waiting for ours.
	uint32_t sumTick[4], sumVal[4];
	uint8_t sumPos;
	uint32_t remTick[LS_SLOTS], remVal[LS_SLOTS];
	uint8_t remWait;                // slots with a checksum waiting
	uint32_t sumsCompared;

	// The stall: when the next tick couldn't run, and who the host says
	// it waits for.
	uint32_t stallSince;
	uint8_t stalled, waitMask;

	// The end of a timed game: the turns every device needs to reach it,
	// when this one got there, and for a joiner whether the host has ended.
	uint32_t endTurn, finishAt;
	uint8_t finishing, ended;

	// Why it stopped. For a desync: the tick, our checksum and theirs.
	uint8_t stop, badSlot;
	uint32_t badTick, badLocal, badRemote;
} Lockstep;

// The host: opens the lobby on an open link end, with its own name.
void SEC_NET ls_host_open(Lockstep *ls, const Link *link, const char *name);
// The host: what joiners see in the lobby, and where the map's bytes come
// from for those who don't have it.
void SEC_NET ls_host_lobby(Lockstep *ls, const char *mapName, uint8_t bots, uint8_t teams,
	LsMapGet get, void *ctx);
// The host starts the game: start says how (numPlayers is set from the
// players seated, names filled in), sent to everyone. The host's own game
// must be set up from it (ls_setup) before its first ls_tick.
void SEC_NET ls_host_start(Lockstep *ls, const LsStart *start);
// A joiner: asks the host on link for a seat under name.
void SEC_NET ls_join_open(Lockstep *ls, const Link *link, const char *name);
// A joiner whose START came (state LS_STARTING) has set up its game from
// ls->start and is ready to play.
void SEC_NET ls_join_ready(Lockstep *ls);
// A joiner whose START came but who lacks the map asks the host for it.
// fetch is LS_MAP_GOT once all of it is in and its CRC matches START.
void SEC_NET ls_join_fetch(Lockstep *ls, LsMapPut put, void *ctx);

// The GameSetup a start gives.
void SEC_NET ls_setup(const LsStart *start, GameSetup *setup);

// Takes what has arrived and sends what's due. Call it often, at least
// before and after running ticks.
void SEC_NET ls_poll(Lockstep *ls, uint32_t now);

// Whether the next tick can run: its turn is complete.
uint8_t SEC_NET ls_ready(Lockstep *ls);

// Runs the next tick of g with this device's player's keys for it, which
// go out with the turn delay later. Only when ls_ready.
void SEC_NET ls_tick(Lockstep *ls, GameState *g, const GameInput *local);

// Finishes only a partially run buffered turn, then hands the other humans
// to bots. Returns 0 for an unsafe state, 1 for offline play, 2 at game end.
// No peer acknowledgement or rollback is implied. Call before closing the link.
uint8_t SEC_NET ls_offline(Lockstep *ls, GameState *g);

// The slots the game is waiting for when stalled for LS_WAIT_MS or more,
// 0 when not waiting.
uint8_t SEC_NET ls_waiting(const Lockstep *ls);

// The longest smoothed round trip to the other devices, ms, 0 if not
// measured yet; and the input delay that suits a round trip.
uint16_t SEC_NET ls_rtt(const Lockstep *ls);
uint8_t SEC_NET ls_delay_for(uint16_t rttMs);

// Leaves the game. The host stops it for everyone; a joiner's tank goes to
// a bot. After ls_finish the host ends the game for everyone and a joiner
// goes quietly.
void SEC_NET ls_leave(Lockstep *ls);

// This device's game is over (g->over). Keep polling until ls_finished,
// then ls_leave: the host waits until every player has the turns to the
// end, a joiner until the host ends the game, LS_END_MS at most.
void SEC_NET ls_finish(Lockstep *ls, const GameState *g);
uint8_t SEC_NET ls_finished(const Lockstep *ls);

// The checksum compared between devices: the state and a sixteenth of the
// terrain and known mines, a different part at each check.
uint32_t SEC_NET ls_checksum(const GameState *g);

#endif
