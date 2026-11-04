// PalmBolo game rules. Plain C, integers only, no Palm headers.
// The state has no pointers, and the checksum is computed field by field in
// a fixed byte order so the 68k and the host get the same value.
//
// The rules follow WinBolo 2 (src/bolo/), Copyright (c) 1998-2026 John
// Morrison, GPLv3. This version Copyright (c) 2026 the PalmBolo authors,
// GPLv3.

#ifndef GAME_H
#define GAME_H

#include <stdint.h>
#include "map.h"
#include "rules.h"

// Code segments on the Palm. The rules are split by area so each segment
// has room to grow; game.c, util.c and map.c stay in the first. A call into
// another segment must see that segment's attribute on the declaration.
#ifdef __palmos__
#define SEC_TANK    __attribute__((section("tank")))
#define SEC_SHELLS  __attribute__((section("shells")))
#define SEC_PILLS   __attribute__((section("pills")))
#define SEC_BUILDER __attribute__((section("builder")))
#else
#define SEC_TANK
#define SEC_SHELLS
#define SEC_PILLS
#define SEC_BUILDER
#endif

#define GAME_TICKS_PER_SECOND 50
#define GAME_MAX_PLAYERS    16      // the player and owner tables
#define GAME_MAX_TANKS      1       // tanks playing now, up to GAME_MAX_PLAYERS later
#define GAME_MAX_SHELLS     24
#define GAME_MAX_EXPL       32      // small explosions (drawn only)
#define GAME_MAX_TKEXPL     8       // dying tank wrecks
#define GAME_MAX_MINEEXP    32      // mines about to go off
#define GAME_MAX_FLOOD      32      // craters about to flood
#define GAME_MAX_DAMAGE     48      // per kind of damaged square
#define GAME_NO_PLAYER      255

// Keys held this tick, one player. The Palm layer fills this in from the
// buttons; the rules see nothing else.
enum {
	IN_LEFT = 0x01,
	IN_RIGHT = 0x02,
	IN_FASTER = 0x04,
	IN_SLOWER = 0x08,
	IN_FIRE = 0x10,
	IN_MINE = 0x20,
	IN_SIGHT_NEAR = 0x40,   // one step nearer this tick
	IN_SIGHT_FAR = 0x80     // one step further this tick
};
enum {
	OPT_AUTOSLOW = 0x01,    // slow down when no speed key is held
	OPT_AUTOSIGHT = 0x02    // hide the gunsight at full range, show it nearer
};
// A builder order rides along with the keys: build is BUILD_NONE or one
// of the actions, for the square buildX, buildY.
enum {
	BUILD_NONE = 0,
	BUILD_TREE, BUILD_ROAD, BUILD_WALL, BUILD_PILL, BUILD_MINE
};
typedef struct GameInput {
	uint8_t keys;
	uint8_t opts;
	uint8_t build;
	uint8_t buildX, buildY;
} GameInput;

enum { BOAT_OFF = 0, BOAT_IN = 1 };

// Game types: what a tank starts with (WinBolo's gameTypeGetItems) and how
// a start is chosen. Open: full shells, mines and trees. Tournament: shells
// by the share of bases still neutral, no mines or trees. Strict
// tournament: armour only.
enum { GAME_OPEN = 0, GAME_TOURNAMENT = 1, GAME_STRICT = 2 };

// Chosen by the host for a game.
typedef struct GameSetup {
	uint8_t gameType;
	uint8_t hiddenMines;    // other players' mines aren't shown (drawing only)
	uint8_t numPlayers;     // 1 to GAME_MAX_TANKS, player n drives tank n
	uint8_t pad;
	uint16_t allies[GAME_MAX_PLAYERS];  // bit n set: allied to player n
} GameSetup;

// A seat in the game (WinBolo's players and allience).
typedef struct Player {
	uint8_t inUse, pad;
	uint16_t allies;        // bit n set: allied to player n
} Player;

typedef struct Tank {
	uint16_t x, y;          // world units
	uint16_t angle;         // 1/32 bradians, 0..8192
	int16_t speed;          // quarter world units a tick
	int32_t bumpX, bumpY;   // shell knockback, moved >> 9 a tick
	uint16_t deathWait;     // ticks until a respawn
	uint16_t deaths;
	uint8_t armour, shells, mines, trees;
	uint8_t destroyed, boat, lastRiverX, lastRiverY;
	uint8_t reload, sightLen, showSight, opts;
	uint8_t justFired, waterCount, residual, firstLeft;
	uint8_t firstRight, obstructed, player, numCarry;
	uint8_t carry[MAP_MAX_PILLS];   // pills carried, newest first
} Tank;

// The builder (WinBolo's lgm). Actions are BUILD_* less one, so LGM_TREE
// is 0, plus the boat a wall on a river turns into.
enum { LGM_TREE, LGM_ROAD, LGM_BUILDING, LGM_PILL, LGM_MINE, LGM_BOAT, LGM_IDLE };
enum { LGM_STATE_IDLE, LGM_STATE_GOING, LGM_STATE_RETURN };
#define LGM_NO_PILL         255
#define LGM_HELICOPTER_FRAME 3
typedef struct Builder {
	uint16_t x, y;          // world units
	uint16_t destX, destY;  // where he walks to, or flies to when dead
	uint8_t state, waitTime, inTank, isDead;
	uint8_t numTrees, numMines, pill, action;   // pill: index carried, or LGM_NO_PILL
	uint8_t nextX, nextY, nextAction, frame;    // frame 0-2 walking, 3 parachute
	uint8_t blessX, blessY, onTop, obstructed;
} Builder;

typedef struct Shell {
	uint16_t x, y;
	uint16_t angle;
	int16_t stepX, stepY;   // 1/256 world units a tick
	int16_t accX, accY;     // fractions not yet moved
	uint8_t length;         // ticks left
	uint8_t owner;          // player, or GAME_NO_PLAYER for a pill's shell
	uint8_t onBoat, pad;
} Shell;

typedef struct Explosion {
	uint16_t x, y;          // world units of the centre
	uint8_t frame;          // counts down to 1
	uint8_t pad;
} Explosion;

typedef struct TankExplosion {
	uint16_t x, y;
	uint16_t angle;
	uint8_t length, big, creator, pad;
} TankExplosion;

// A square with a timer or a life count: mines about to go off, craters
// about to flood, damaged buildings, grass, swamp and rubble.
typedef struct Square {
	uint8_t x, y, n;
} Square;

typedef struct SquareList {
	uint8_t count;
	Square item[GAME_MAX_DAMAGE];
} SquareList;

enum { DMG_BUILDING, DMG_GRASS, DMG_SWAMP, DMG_RUBBLE, DMG_KINDS };

typedef struct GameState {
	uint32_t rng;           // random number generator state, never 0
	uint32_t tick;          // game ticks run so far
	uint8_t numTanks, numShells, numExpl, numTkExpl;
	uint8_t numMineExp, numFlood, explTimer, tkExplTimer;
	uint8_t halfShell, halfMine;
	uint8_t gameType, hiddenMines;
	uint8_t baseTime[MAP_MAX_BASES];        // ticks until a base gives again
	uint8_t baseStopped[MAP_MAX_BASES];     // no tank was on it last tick
	uint16_t baseTimer[GAME_MAX_PLAYERS];   // per player, ticks to the next restock
	Player players[GAME_MAX_PLAYERS];
	Tank tanks[GAME_MAX_TANKS];
	Shell shells[GAME_MAX_SHELLS];          // [0] is the newest
	Explosion expl[GAME_MAX_EXPL];          // [0] is the newest
	TankExplosion tkExpl[GAME_MAX_TKEXPL];  // [0] is the newest
	Square mineExp[GAME_MAX_MINEEXP];       // [0] is the newest
	Square flood[GAME_MAX_FLOOD];           // [0] is the oldest
	SquareList damage[DMG_KINDS];           // [0] is the newest
	Builder men[GAME_MAX_TANKS];            // tank n's builder
	uint16_t treeSeed;      // tree growth's own generator, seeded from rng
	int16_t treeTime, treeScore;            // ticks to the next tree, best score
	uint8_t treeX, treeY;   // where it grows
	MapInfo map;            // pills, bases and starts of the loaded map
} GameState;

// Sets up a new game on the loaded map, with a tank for each player at a
// start. A seed of 0 is replaced so the generator never sticks. setup may
// be NULL for an open game with one player. The map info must be filled in
// (map_parse) before this.
void game_init(GameState *s, uint32_t seed, const GameSetup *setup);

// Whether players a and b are the same or allied (WinBolo's
// playersIsAllie). GAME_NO_PLAYER is nobody's ally.
uint8_t game_allied(const GameState *s, uint8_t a, uint8_t b);

// How a pill or base looks to player viewer.
enum { OWN_NEUTRAL, OWN_SELF, OWN_ALLY, OWN_ENEMY };
uint8_t game_owner_kind(const GameState *s, uint8_t owner, uint8_t viewer);

// Advances one tick. in has one entry per tank.
void game_tick(GameState *s, const GameInput *in);

// Next random number from the state's generator, and one below n.
uint32_t game_random(GameState *s);
uint16_t game_random_below(GameState *s, uint16_t n);

// CRC-32 over the state's fields in big-endian order, and over the terrain.
uint32_t game_checksum(const GameState *s);
uint32_t game_terrain_checksum(void);

// Squares whose terrain changed since the last call, for the view. Returns
// how many were put in xy (x then y, two bytes each, up to max), or 255
// when more changed than were kept and the whole view needs a redraw.
uint8_t game_take_changes(uint8_t *xy, uint8_t max);

// Gunsight square of a tank in world units (where a shell at this range
// lands), and whether it is shown.
void SEC_TANK game_gunsight(const GameState *s, uint8_t t, uint16_t *wx, uint16_t *wy);

// Whether the player whose screen this is (game_set_viewer, 0 by default)
// is shown the mine at a square. With hidden mines on that is only mines
// they laid or saw, or an ally laid; drawing only, the rules see them all.
void game_set_viewer(uint8_t player);
uint8_t game_mine_shown(const GameState *s, uint8_t x, uint8_t y);

// Terrain at a square as the rules see it: deep sea in the mined border.
uint8_t game_terrain(uint8_t x, uint8_t y);

// Sets a square's terrain and notes it for game_take_changes.
void game_set_terrain(uint8_t x, uint8_t y, uint8_t t);

#endif
