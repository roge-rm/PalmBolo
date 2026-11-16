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
#define SEC_BOTS    __attribute__((section("bots")))
// The scripted games, debug builds only.
#define SEC_SCRIPT  __attribute__((section("detect")))
// Setup, map parsing and the checksum: run rarely.
#ifndef SEC_COLD
#define SEC_COLD    __attribute__((section("cold")))
#endif
#else
#define SEC_COLD
#define SEC_SCRIPT
#define SEC_TANK
#define SEC_SHELLS
#define SEC_PILLS
#define SEC_BUILDER
#define SEC_BOTS
#endif

#define GAME_TICKS_PER_SECOND 50
#define GAME_MAX_PLAYERS    16      // the player and owner tables
#define GAME_MAX_TANKS      8       // the host and up to 7 more, players or bots
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

// How hard a bot plays. BOT_OFF is a tank driven by its player.
enum { BOT_OFF = 0, BOT_EASY, BOT_NORMAL, BOT_HARD, BOT_LEVELS };

// Chosen by the host for a game.
typedef struct GameSetup {
	uint8_t gameType;
	uint8_t hiddenMines;    // other players' mines aren't shown (drawing only)
	uint8_t numPlayers;     // 1 to GAME_MAX_TANKS, player n drives tank n
	uint8_t numBots;        // bots after the players, numPlayers + numBots tanks in all
	uint8_t botLevel;       // BOT_EASY to BOT_HARD
	uint8_t timeLimit;      // minutes, 0 for none
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
	uint16_t kills;         // other tanks it shot dead (WinBolo's round stats)
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

// A bot driving a tank (bot.c). Everything it remembers is here, bytes
// only, so the checksum can take it whole and any Palm can hand a tank to
// a bot at any tick.
#define BOT_WAYS    12      // path turning points kept, way[0] next
typedef struct Bot {
	uint8_t level;          // BOT_OFF or BOT_EASY..BOT_HARD
	uint8_t goal;           // what it is doing (BG_* in bot.c)
	uint8_t target;         // the base, pill or tank of the goal
	uint8_t goalX, goalY;   // the square it is making for
	uint8_t think;          // ticks to the next look round
	uint8_t mode;           // how it drives this tick (BM_* in bot.c)
	uint8_t modeTime;       // ticks left in the mode
	uint8_t veer;           // heading of a veer, 1/32 of a turn
	uint8_t foe;            // tank it is fighting, or 255
	uint8_t react;          // ticks before it fires at a new foe
	uint8_t aimErr;         // signed, 1/32 bradians added to its aim
	uint8_t clear;          // the line to what it aims at was clear at the last look
	uint8_t want;           // waiting for a path search
	uint8_t pathX, pathY;   // goal the waypoints lead to
	uint8_t pathOX, pathOY; // keep a useful maze window while following its route
	uint8_t boat;           // on a boat when the path was made
	uint8_t fails;          // searches in a row that got no nearer
	uint8_t skip, skipTime; // goal (kind << 5 | index) left alone for a while
	uint8_t lastX, lastY;   // square at the last stuck check
	uint8_t stuck;          // stuck checks with no new square
	uint8_t minesLaid;
	uint8_t buildWait;      // ticks before another builder order
	// What it steers by between looks (bot.c drives in full every few
	// ticks and keeps these in between).
	uint8_t wantHi, wantLo; // heading to turn to
	uint8_t tsp;            // speed to make for, quarters a tick
	uint8_t dead;           // turns only when further off than this
	uint8_t steerBits;      // SB_* in bot.c
	uint8_t sightWant;      // gunsight length to step to, 0 for none
	uint8_t fireHi, fireLo; // heading it shoots along when facing it
	uint8_t fireRange;      // range of that shot, 16 world units
	uint8_t lastArmour;     // armour at the last tick, to see a hit
	uint8_t lookX, lookY;   // square at the last full look
	uint8_t boatTask, boatX, boatY; // 1 board, 2 build at a river launch
	uint8_t wayN;
	uint8_t way[BOT_WAYS][2];
} Bot;

// One bounded Dijkstra search, shared by all bots. Heap ties use cell index.
#define BOT_WIN     32
#define BOT_CELLS   (BOT_WIN * BOT_WIN)
#define BOT_NO_CELL 65535U
typedef struct BotSearch {
	uint8_t who, phase;     // owner + 1; 2 zones, 0 search, 1 trace
	uint8_t ox, oy, goalX, goalY, boat, next;
	uint8_t bestH, wayN, near, zone, lastDir, wayPos;
	uint8_t launchKind, launchX, launchY, startH;
	uint16_t best, cur, count, launch, launchScore;
	uint8_t way[BOT_WAYS][2];
	uint8_t cell[BOT_CELLS / 2]; // 0 new, 1..8 parent direction, 9 start, 15 solid
	uint8_t danger[BOT_CELLS / 8];
	uint16_t distance[BOT_CELLS];
	uint16_t heap[BOT_CELLS];
	uint16_t position[BOT_CELLS]; // BOT_NO_CELL when absent from the heap
} BotSearch;

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
	uint8_t botMask;        // bit n: a bot drives tank n
	uint8_t over;           // the time limit has run out
	uint32_t timeLimit;     // ticks the game lasts, 0 for no limit
	Bot bots[GAME_MAX_TANKS];
	BotSearch search;
	MapInfo map;            // pills, bases and starts of the loaded map
} GameState;

// Sets up a new game on the loaded map, with a tank for each player at a
// start. A seed of 0 is replaced so the generator never sticks. setup may
// be NULL for an open game with one player. The map info must be filled in
// (map_parse) before this.
void SEC_COLD game_init(GameState *s, uint32_t seed, const GameSetup *setup);

// Whether players a and b are the same or allied (WinBolo's
// playersIsAllie). GAME_NO_PLAYER is nobody's ally.
uint8_t game_allied(const GameState *s, uint8_t a, uint8_t b);

// How a pill or base looks to player viewer.
enum { OWN_NEUTRAL, OWN_SELF, OWN_ALLY, OWN_ENEMY };
uint8_t game_owner_kind(const GameState *s, uint8_t owner, uint8_t viewer);

// Advances one tick; over is set when the time limit is reached, and the
// tick that sets it is the last. in has one entry per tank, numTanks of them; a tank
// a bot drives ignores its entry.
void game_tick(GameState *s, const GameInput *in);

// Hands tank t to a bot of level, from where it is now, or back to its
// player with BOT_OFF. Every Palm in a game must do this on the same tick.
void game_set_bot(GameState *s, uint8_t t, uint8_t level);

// The keys the bots make for this tick, as game_tick starts with. For
// timing the bots apart from the rules: game_tick with these and botMask
// cleared does the same tick.
void game_bot_inputs(GameState *s, const GameInput *in, GameInput *out);

// Next random number from the state's generator, and one below n.
uint32_t game_random(GameState *s);
uint16_t game_random_below(GameState *s, uint16_t n);

// CRC-32 over the state's fields in big-endian order, and over the terrain
// with the plane of mines known to the rules (MS_KNOWN). The viewer's seen
// mines are left out, as they differ from screen to screen.
uint32_t SEC_COLD game_checksum(const GameState *s);
uint32_t game_terrain_checksum(void);

// Squares whose terrain changed since the last call, for the view. Returns
// how many were put in xy (x then y, two bytes each, up to max), or 255
// when more changed than were kept and the whole view needs a redraw.
uint8_t game_take_changes(uint8_t *xy, uint8_t max);

// Gunsight square of a tank in world units (where a shell at this range
// lands), and whether it is shown.
void SEC_TANK game_gunsight(const GameState *s, uint8_t t, uint16_t *wx, uint16_t *wy);

// The viewer is the player whose screen this is (game_set_viewer, 0 by
// default). It is a player number; game_tank_of gives the tank that player
// drives, or -1 if none.
void game_set_viewer(uint8_t player);
uint8_t game_viewer(void);
int8_t game_tank_of(const GameState *s, uint8_t player);
// The tank the screen follows: the viewer's, or tank 0 if they have none.
uint8_t game_viewer_tank(const GameState *s);

// Whether the viewer is shown the mine at a square. With hidden mines on
// that is only mines they laid or saw, or an ally laid; drawing only, the
// rules see them all.
uint8_t game_mine_shown(const GameState *s, uint8_t x, uint8_t y);

// Terrain at a square as the rules see it: deep sea in the mined border.
uint8_t game_terrain(uint8_t x, uint8_t y);

// Sets a square's terrain and notes it for game_take_changes.
void game_set_terrain(uint8_t x, uint8_t y, uint8_t t);

// Sounds the rules made, where WinBolo calls soundDist. They are kept
// outside the game state for the screen to play, and nothing in the rules
// reads them back. x, y is the map square.
enum {
	SND_SHOOT,          // a tank or pill fired; player is the tank's, or GAME_NO_PLAYER
	SND_SHOT_TREE,      // a tree shot or flattened by a wreck
	SND_SHOT_BUILDING,  // a wall, pill, base or boat hit, or a road shot into river
	SND_HIT_TANK,       // a shell hit a tank; player is the tank hit
	SND_BUBBLES,        // a wading tank lost stock; player is the tank's
	SND_TANK_SINK,      // a tank drowned; player is the tank's
	SND_BIG_EXPLOSION,  // a big wreck went off
	SND_MINE_EXPLOSION, // a mine or a small wreck went off
	SND_LAY_MINE,       // a tank or builder laid a mine
	SND_FARM_TREE,      // the builder cut a tree
	SND_MAN_BUILDING,   // the builder built or repaired
	SND_MAN_DYING,      // the builder was killed
	SND_BASE_CAPTURE,   // a base changed hands (WinBolo has no sound for it)
	SND_KINDS
};
#define GAME_MAX_SOUNDS 16
typedef struct GameSound {
	uint8_t kind, x, y, player;
} GameSound;

// Copies the sounds made since the last call into out (up to max) and
// returns how many. Sounds past GAME_MAX_SOUNDS before a call are dropped.
uint8_t game_take_sounds(GameSound *out, uint8_t max);

#endif
