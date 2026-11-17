// Bots: simple tank drivers that are part of the game rules. Each tick a
// bot makes the keys and builder orders for its tank from the game state
// alone, using only the state's random number generator, so every Palm in
// a network game runs the same bots, and a bot can take over any tank at
// any tick.
//
// A bot looks round every few ticks (think) and picks a goal: refuel at an
// own base when low, fight a tank it sees, else take the nearest base or
// pill worth having, shooting down the pills that guard it first. Paths
// come from one shared search by cost over a 32 square window that runs a
// few cells a tick for each bot (search_step), so the cost of a bot stays
// bounded however many there are. Live pills are shot in runs: drive
// at it firing, veer off when close, come round again.
//
// PalmBolo's own code, Copyright (c) 2026 the PalmBolo authors, GPLv3.
#include "gamepriv.h"
#include "mapstore.h"
#include "util.h"

enum { BG_NONE, BG_BASE, BG_PILL, BG_REFUEL, BG_TANK, BG_PATROL, BG_MAN };
enum { BM_NAV, BM_RUN, BM_VEER, BM_ESCAPE };
enum { BK_BASE = 1, BK_PILL = 2, BK_TANK = 3 };     // kinds for Bot.skip

#define NO_TANK         255
#define NO_BASE         255     // target of a patrol that is not round a base
#define BOT_BUDGET      12      // search cells expanded a tick, all bots together, at least
#define BOT_BUDGET_EACH 3       // or this many for each bot, if more
#define BOT_MAX_CELLS   BOT_CELLS     // maximum cells in one window
#define GUN_RANGE       1600    // world units a bot shoots at a tank from
#define PILL_RUN_RANGE  1700    // starts a run at a pill this near
#define PILL_VEER_RANGE 1024    // turns away from a pill this near
#define BASE_SHOOT_RANGE 1500
#define FULL_SPEED      64      // quarters a tick, the road's top speed
#define MINE_COST       24
// A bot drives in full on one tick in BOT_EVERY, by slot, and at once when
// it is hit, its foe dies or it reaches a new square. In between it steers
// by what it decided. Checks every 24 and 8 ticks fall on its full ticks,
// so BOT_EVERY divides 8.
#define BOT_EVERY       4
// SB_TIME: the last full look counted down modeTime, so the ticks after
// it do too.
enum { SB_ON = 1, SB_KEEP = 2, SB_SEA = 4, SB_FIRE = 8, SB_TIME = 16 };        // Bot.steerBits

// Per level: ticks between looks round, ticks before firing at a new
// foe, most aim error (4/32 bradians), lead on a moving tank (halves),
// squares it takes on a tank at, armour it goes to refuel under, whether
// it chases a tank out of range.
typedef struct Level {
	uint8_t think, react, aimErr, lead, engage, retreat, chase;
} Level;
static const Level kLevel[BOT_LEVELS] = {
	{ 1, 0, 0, 0, 0, 0, 0 },
	{ 25, 25, 40, 0, 5, 25, 0 },
	{ 12, 12, 20, 1, 7, 20, 1 },
	{ 6, 4, 6, 2, 8, 15, 1 }
};

// Directions N, NE, E, SE, S, SW, W, NW.
// Not const, so a 68k reads them off a5 rather than through the code segment.
static int8_t kDX[8] = { 0, 1, 1, 1, 0, -1, -1, -1 };
static int8_t kDY[8] = { -1, -1, 0, 1, 1, 1, 0, -1 };

static inline uint8_t SEC_BOTS level_think(uint8_t l) { return kLevel[l].think; }

static inline uint8_t SEC_BOTS udiff8(uint8_t a, uint8_t b)
{
	return a > b ? (uint8_t)(a - b) : (uint8_t)(b - a);
}

// Squares apart, the larger of the two axes.
static inline uint8_t SEC_BOTS cheb(uint8_t ax, uint8_t ay, uint8_t bx, uint8_t by)
{
	uint8_t dx = udiff8(ax, bx), dy = udiff8(ay, by);

	return dx > dy ? dx : dy;
}

// World units apart, near enough: the larger axis plus half the smaller.
static inline uint16_t SEC_BOTS wdist(uint16_t ax, uint16_t ay, uint16_t bx, uint16_t by)
{
	uint16_t dx = ax > bx ? ax - bx : bx - ax, dy = ay > by ? ay - by : by - ay;

	return dx > dy ? (uint16_t)(dx + (dy >> 1)) : (uint16_t)(dy + (dx >> 1));
}

static inline uint16_t SEC_BOTS mid(uint8_t sq)
{
	return (uint16_t)(((uint16_t)sq << 8) + 128U);
}

// Signed turn from have to want, -4096..4095.
static inline int16_t SEC_BOTS turn_to(uint16_t want, uint16_t have)
{
	int16_t d = (int16_t)((want - have) & (ANGLE_MAX - 1));

	return d >= ANGLE_MAX / 2 ? (int16_t)(d - ANGLE_MAX) : d;
}

// x / d and x % d for a 16-bit d, with divu on a 68k rather than gcc's
// 32-bit divide routines. div32_16 needs the quotient to fit in 16 bits.
static inline uint16_t SEC_BOTS div32_16(uint32_t x, uint16_t d)
{
#ifdef __m68k__
	__asm__("divu %2,%0" : "=d" (x) : "0" (x), "dm" (d));
	return (uint16_t)x;
#else
	return (uint16_t)(x / d);
#endif
}

static inline uint16_t SEC_BOTS mod32_16(uint32_t x, uint16_t d)
{
#ifdef __m68k__
	uint32_t h = x >> 16;

	// The high word's remainder, then that and the low word.
	__asm__("divu %2,%0" : "=d" (h) : "0" (h), "dm" (d));
	h = (h & 0xFFFF0000UL) | (x & 0xFFFF);
	__asm__("divu %2,%0" : "=d" (h) : "0" (h), "dm" (d));
	return (uint16_t)(h >> 16);
#else
	return (uint16_t)(x % d);
#endif
}

static inline uint8_t SEC_BOTS is_wall(uint8_t t)
{
	return t == TERRAIN_BUILDING || t == TERRAIN_HALFBUILDING;
}

static inline uint8_t SEC_BOTS plain(uint8_t t)
{
	return t >= TERRAIN_MINE_FIRST && t <= TERRAIN_MINE_LAST ? (uint8_t)(t - TERRAIN_MINE_ADD) : t;
}

static inline uint8_t SEC_BOTS builder_free(const GameState *s, uint8_t t)
{
	const Builder *m = &s->men[t];

	return m->inTank && !m->isDead && m->action == LGM_IDLE && m->nextAction == LGM_IDLE;
}

// game_allied and game_terrain, here so the hot loops make no calls into
// code segment 1, which cost a lot on a 68k.
static inline uint8_t SEC_BOTS allied(const GameState *s, uint8_t a, uint8_t b)
{
	if (a == b)
		return 1;
	if (a >= GAME_MAX_PLAYERS || b >= GAME_MAX_PLAYERS || !s->players[a].inUse)
		return 0;
	return (uint8_t)((s->players[a].allies >> b) & 1);
}

static inline uint8_t SEC_BOTS terrain(uint8_t x, uint8_t y)
{
	if (x > MAP_EDGE_LOW && x < MAP_EDGE_HIGH && y > MAP_EDGE_LOW && y < MAP_EDGE_HIGH)
		return mapstore_row(y)[x];
	return TERRAIN_DEEP_SEA;
}

// terrain, keeping the last row fetched, for lines across the map.
typedef struct Row1 {
	uint8_t y;
	const uint8_t *r;
} Row1;

static inline uint8_t SEC_BOTS terrain_c(Row1 *c, uint8_t x, uint8_t y)
{
	if (x > MAP_EDGE_LOW && x < MAP_EDGE_HIGH && y > MAP_EDGE_LOW && y < MAP_EDGE_HIGH) {
		if (!c->r || c->y != y) {
			c->r = mapstore_row(y);
			c->y = y;
		}
		return c->r[x];
	}
	return TERRAIN_DEEP_SEA;
}

// ---- path search ----

// The Palm build runs expand in 68k assembly (expand68k.S, below expand).
// EXTRA=-DEXPAND_C builds it with the C alone.
#if defined(__m68k__) && !defined(EXPAND_C)
#define EXPAND_ASM
#endif

#if defined(SEARCH_PARTS) && defined(__m68k__)
// Debug: the Palm build times each search step and counts the cells.
extern void search_mark(uint8_t end) __attribute__((section("ui")));
extern unsigned long gSearchCells;
#define SEARCH_MARK(e) search_mark(e)
#define SEARCH_CELL() (gSearchCells++)
// Timer 1 counts read directly, for parts well under a system tick.
extern unsigned short gSearchDiv;
extern unsigned long gSearchExpand, gSearchTake, gSearchSlow, gSearchSlowN, gSearchLaunch, gSearchLaunchN;
// gSearchDiv is 0 where the timer isn't there (OS 5), and nothing is read.
#define SR_NOW() (gSearchDiv ? *(volatile uint16_t *)0xFFFFF608UL : 0)
#define SR_ADD(sum, t0) \
	do { \
		if (gSearchDiv) { \
			uint16_t t1_ = SR_NOW(); \
			(sum) += t1_ >= (t0) ? t1_ - (t0) : t1_ + gSearchDiv - (t0); \
		} \
	} while (0)
#else
#define SEARCH_MARK(e) ((void)0)
#define SEARCH_CELL() ((void)0)
#endif

static inline uint8_t SEC_BOTS pill_hostile(const GameState *s, const MapPill *p, uint8_t player)
{
	return p->active && !p->inTank && (p->owner == MAP_NEUTRAL || !allied(s, p->owner, player));
}

static inline uint8_t SEC_BOTS nib(const BotSearch *q, uint16_t i)
{
	uint8_t v = q->cell[i >> 1];

	return (uint8_t)((i & 1) ? v >> 4 : v & 15);
}

static inline void SEC_BOTS set_nib(BotSearch *q, uint16_t i, uint8_t v)
{
	uint8_t *p = &q->cell[i >> 1];

	*p = (uint8_t)((i & 1) ? (*p & 0x0F) | (v << 4) : (*p & 0xF0) | v);
}

// Map rows fetched as needed, 16 from y0 on, for the squares round a pill.
// Terrain as game_terrain gives it.
typedef struct Rows16 {
	uint8_t y0;
	const uint8_t *r[16];
} Rows16;

static inline uint8_t SEC_BOTS terrain16(Rows16 *c, uint8_t x, uint8_t y)
{
	uint8_t i = (uint8_t)(y - c->y0);

	if (x <= MAP_EDGE_LOW || x >= MAP_EDGE_HIGH || y <= MAP_EDGE_LOW || y >= MAP_EDGE_HIGH)
		return TERRAIN_DEEP_SEA;
	if (i >= 16)
		return mapstore_row(y)[x];
	if (!c->r[i])
		c->r[i] = mapstore_row(y);
	return c->r[i][x];
}

// Land a tank crosses, by terrain 0 to 9 (no mines): 0 for walls, else
// 1 plus the ticks it waits in the queue.
// Not static, as expand68k.S reads it too.
uint8_t kLandCost[10] = { 0, 6, 5, 5, 1, 3, 5, 1, 0, 1 };

// What a square costs to cross: 0 it can't be, else 1 plus the ticks it
// waits in the queue. Walls are out, and off a boat so is deep sea and
// anything beside it, as a fast tank can't stop short. A known mine costs
// a lot, so a tank shut in by mines still gets out. hidden is the game's
// hiddenMines. cost_in is for a square more than a square inside the map's
// edge band, which the caller has checked.
static inline uint8_t SEC_BOTS cost_in(uint8_t hidden, uint8_t x, uint8_t y, uint8_t boat)
{
	const uint8_t *const *rows = &mapstore_rows[y];
	const uint8_t *row = rows[0];
	uint8_t t = row[x];

	if (t >= TERRAIN_MINE_FIRST && t <= TERRAIN_MINE_LAST) {
		if (!hidden || mapstore_flag(MS_KNOWN, x, y))
			return MINE_COST;
		t = (uint8_t)(t - TERRAIN_MINE_ADD);
	}
	if (is_wall(t))
		return 0;
	// A boat is fast on water and leaves the boat behind on land.
	if (boat)
		return t == TERRAIN_RIVER || t == TERRAIN_DEEP_SEA || t == TERRAIN_BOAT ? 1 : 2;
	if (t == TERRAIN_DEEP_SEA || row[x - 1] == TERRAIN_DEEP_SEA || row[x + 1] == TERRAIN_DEEP_SEA ||
	    rows[-1][x] == TERRAIN_DEEP_SEA || rows[1][x] == TERRAIN_DEEP_SEA)
		return 0;
	return t < 10 ? kLandCost[t] : 1;
}

static inline uint8_t SEC_BOTS cost_at(uint8_t hidden, uint8_t x, uint8_t y, uint8_t boat)
{
	if (x <= MAP_EDGE_LOW + 1 || x >= MAP_EDGE_HIGH - 1 || y <= MAP_EDGE_LOW + 1 ||
	    y >= MAP_EDGE_HIGH - 1)
		return 0;
	return cost_in(hidden, x, y, boat);
}

static uint8_t SEC_BOTS cost(const GameState *s, uint8_t x, uint8_t y, uint8_t boat)
{
	return cost_at(s->hiddenMines, x, y, boat);
}

// Keep both endpoints in the window whenever they fit. For farther goals,
// leave four squares behind the tank for a detour.
static uint8_t SEC_BOTS window_origin(uint8_t from, uint8_t to)
{
	int16_t delta = (int16_t)to - from, o;

	if (delta > -BOT_WIN && delta < BOT_WIN)
		o = (int16_t)(((int16_t)from + to - (BOT_WIN - 1)) / 2);
	else
		o = (int16_t)(from - BOT_WIN / 2 + (delta > 0 ? 12 : -12));
	if (o < 0)
		o = 0;
	if (o > 256 - BOT_WIN)
		o = 256 - BOT_WIN;
	return (uint8_t)o;
}

// Half the width of a pill's reach, 8 squares, on each row from its own.
static const uint8_t kReach[9] = { 8, 7, 7, 7, 6, 6, 5, 3, 0 };
#define ZONE_WAIT 8    // extra queue ticks a square in a pill's reach costs

// Marks the squares in reach of live hostile pill i in the window as
// costly, leaving ones already marked. Returns 1 if it marked any.
static uint8_t SEC_BOTS mark_zone(GameState *s, uint8_t i)
{
	BotSearch *q = &s->search;
	const MapPill *p = &s->map.pills[i];
	int16_t px, py, y, y0, y1, x0, x1;

	if (!pill_hostile(s, p, s->tanks[q->who - 1].player) || p->armour == 0)
		return 0;
	px = (int16_t)(p->x - q->ox);
	py = (int16_t)(p->y - q->oy);
	if (px < -8 || px >= BOT_WIN + 8 || py < -8 || py >= BOT_WIN + 8)
		return 0;
	y0 = py < 8 ? 0 : (int16_t)(py - 8);
	y1 = py + 8 >= BOT_WIN ? BOT_WIN - 1 : (int16_t)(py + 8);
	for (y = y0; y <= y1; y++) {
		uint8_t r = kReach[y > py ? y - py : py - y], *p;
		uint32_t m;

		x0 = (int16_t)(px - r);
		x1 = (int16_t)(px + r);
		if (x0 < 0)
			x0 = 0;
		if (x1 >= BOT_WIN)
			x1 = BOT_WIN - 1;
		if (x0 > x1)
			continue;
		// Bits x0 to x1 of the row's four bytes, the lowest x in the lowest
		// bit of the first byte.
		m = (0xFFFFFFFFUL >> (31 - x1)) & (0xFFFFFFFFUL << x0);
		p = &q->danger[y * (BOT_WIN / 8)];
		p[0] |= (uint8_t)m;
		p[1] |= (uint8_t)(m >> 8);
		p[2] |= (uint8_t)(m >> 16);
		p[3] |= (uint8_t)(m >> 24);
	}
	return 1;
}

// How far a window cell is from the goal. With the goal out of the window
// only a cell on an edge facing it is taken at its distance, the rest come
// 32 later, so a partial path stops where the next window can go on from.
static inline uint8_t SEC_BOTS cell_h(const BotSearch *q, uint16_t i)
{
	uint8_t lx = (uint8_t)(i & (BOT_WIN - 1)), ly = (uint8_t)(i >> 5);
	uint8_t gx = (uint8_t)(q->goalX - q->ox), gy = (uint8_t)(q->goalY - q->oy);
	uint8_t h = cheb((uint8_t)(q->ox + lx), (uint8_t)(q->oy + ly), q->goalX, q->goalY);

	// gx, gy of 32 to 143 are past the high edge, 144 to 255 past the low one.
	if ((gx >= BOT_WIN || gy >= BOT_WIN) &&
	    !(gx >= BOT_WIN && lx == (gx < 144 ? BOT_WIN - 1 : 0)) &&
	    !(gy >= BOT_WIN && ly == (gy < 144 ? BOT_WIN - 1 : 0)))
		h = (uint8_t)(h > 255 - 32 ? 255 : h + 32);
	return h;
}

// Distances to the goal from each window column and row, for the origin,
// goal and near they were last made for: gRawX and gRawY as they are, gHX
// and gHY less near (0 at least), less the least that is anywhere in the
// window (0 at least again), gTX and gTY less their least. They follow from
// those five alone, so they are the same on every Palm and aren't part of
// the state. Not static, as expand68k.S reads them too.
uint8_t gRawX[BOT_WIN], gRawY[BOT_WIN], gHX[BOT_WIN], gHY[BOT_WIN], gTX[BOT_WIN], gTY[BOT_WIN];
static uint8_t gTabOX, gTabOY, gTabGX, gTabGY, gTabNear, gTabOn;
// As cell_h: with the goal out of the window (gFar), only cells in the
// column gEdgeX or row gEdgeY facing it are taken at their distance. 255
// for none.
uint8_t gFar, gEdgeX, gEdgeY;

static void SEC_BOTS tables(const BotSearch *q)
{
	uint8_t i, near = q->near, gx, gy, hx = 255, hy = 255, tx = 255, ty = 255;

	if (gTabOn && gTabOX == q->ox && gTabOY == q->oy && gTabGX == q->goalX && gTabGY == q->goalY &&
	    gTabNear == near)
		return;
	for (i = 0; i < BOT_WIN; i++) {
		uint8_t dx = udiff8((uint8_t)(q->ox + i), q->goalX), dy = udiff8((uint8_t)(q->oy + i), q->goalY);

		gRawX[i] = dx;
		gRawY[i] = dy;
		gHX[i] = dx > near ? (uint8_t)(dx - near) : 0;
		gHY[i] = dy > near ? (uint8_t)(dy - near) : 0;
		if (gHX[i] < hx)
			hx = gHX[i];
		if (gHY[i] < hy)
			hy = gHY[i];
		if (dx < tx)
			tx = dx;
		if (dy < ty)
			ty = dy;
	}
	// The least distance to go in the window is the larger of the two
	// least, so a cell's is that and 0 to 31.
	if (hy > hx)
		hx = hy;
	for (i = 0; i < BOT_WIN; i++) {
		gHX[i] = gHX[i] > hx ? (uint8_t)(gHX[i] - hx) : 0;
		gHY[i] = gHY[i] > hx ? (uint8_t)(gHY[i] - hx) : 0;
		gTX[i] = (uint8_t)(gRawX[i] - tx);
		gTY[i] = (uint8_t)(gRawY[i] - ty);
	}
	gTabOX = q->ox;
	gTabOY = q->oy;
	gTabGX = q->goalX;
	gTabGY = q->goalY;
	gTabNear = near;
	gTabOn = 1;
	gx = (uint8_t)(q->goalX - q->ox);
	gy = (uint8_t)(q->goalY - q->oy);
	gFar = gx >= BOT_WIN || gy >= BOT_WIN;
	gEdgeX = gx >= BOT_WIN ? (gx < 144 ? BOT_WIN - 1 : 0) : 255;
	gEdgeY = gy >= BOT_WIN ? (gy < 144 ? BOT_WIN - 1 : 0) : 255;
}

// Cells come off the heap in order of cost so far plus distance to go (f),
// then less distance to go, which is more cost so far, then less distance
// along both axes, which breaks ties towards the goal, then the lower cell.
// Chebyshev distance is a consistent lower bound for eight-way steps costing
// at least one, so a cell is settled when it first comes off.
// A key holds all four, f less the window's least distance to go and less
// fbase in bits 21-31 (at most 2047), distance to go less the same in bits
// 16-20, the axes' distance less their least in bits 10-15 and the cell in
// bits 0-9, so keys compare as one number. Needs tables() run for the
// search first.
static inline uint8_t SEC_BOTS cell_a(uint16_t cell)
{
	uint8_t a = gHX[cell & (BOT_WIN - 1)], b = gHY[cell >> 5];

	return b > a ? b : a;
}

// A cell's f less fbase, over 2047 if it doesn't fit a key.
static inline uint16_t SEC_BOTS cell_f(const BotSearch *q, uint16_t cell)
{
	return (uint16_t)(q->distance[cell] + cell_a(cell) - q->fbase);
}

static inline uint32_t SEC_BOTS cell_key(const BotSearch *q, uint16_t cell)
{
	uint8_t lx = (uint8_t)(cell & (BOT_WIN - 1)), ly = (uint8_t)(cell >> 5);
	uint8_t a = cell_a(cell);
	uint16_t f = cell_f(q, cell);

	// Only a heap made outside a search can be this far apart.
	if (f > 2047)
		f = 2047;
	return ((uint32_t)(uint16_t)((f << 5) | a) << 16) |
		(uint16_t)(((uint16_t)(gTX[lx] + gTY[ly]) << 10) | cell);
}

// Heap places are held as byte offsets in an int16_t, as a 68k indexes by a
// signed word and has no scaled index.
#define HEAP_AT(h, o) (*(uint32_t *)((uint8_t *)(h) + (o)))

static inline void SEC_BOTS heap_put(BotSearch *q, uint32_t key)
{
	uint32_t *heap = q->heap, v;
	int16_t at = (int16_t)(q->count++ << 2), parent;

	while (at) {
		parent = (int16_t)(((at - 4) >> 1) & ~3);
		v = HEAP_AT(heap, parent);
		if (v < key)
			break;
		HEAP_AT(heap, at) = v;
		at = parent;
	}
	HEAP_AT(heap, at) = key;
}

static inline uint8_t SEC_BOTS is_settled(const BotSearch *q, uint16_t cell)
{
	return (uint8_t)(q->settled[cell >> 3] & (1 << (cell & 7)));
}

// Drops the entries left behind by cheaper arrivals.
static void SEC_BOTS heap_compact(BotSearch *q)
{
	uint16_t i, n = q->count;

	q->count = 0;
	for (i = 0; i < n; i++) {
		uint32_t k = q->heap[i];
		uint16_t c = (uint16_t)(k & (BOT_CELLS - 1));

		if (!is_settled(q, c) && k == cell_key(q, c))
			heap_put(q, k);
	}
}

// Moves fbase to the least f in the heap, or f if that is less, and the
// keys with it. In a search the f in the heap and a cell pushed lie
// within a step and the distance to go of the cell expanded, far less
// than 2047, so they all fit again.
static void SEC_BOTS heap_rebase(BotSearch *q, uint16_t f)
{
	uint16_t i, base = f;
	uint32_t sub;

	if (q->count && (uint16_t)(q->fbase + (q->heap[0] >> 21)) < base)
		base = (uint16_t)(q->fbase + (q->heap[0] >> 21));
	// Modulo 2^32, so a base below fbase moves the keys up.
	sub = (uint32_t)(uint16_t)(base - q->fbase) << 21;
	for (i = 0; i < q->count; i++)
		q->heap[i] -= sub;
	q->fbase = base;
}

// Adds a cell at its distance now. A full heap is compacted first, which
// leaves room: there is one entry for each cell reached and not settled,
// and the cell expanded is settled.
static void SEC_BOTS heap_push(BotSearch *q, uint16_t cell)
{
	if (q->count >= BOT_CELLS) {
		heap_compact(q);
		if (q->count >= BOT_CELLS)
			return;
	}
	if (cell_f(q, cell) > 2047)
		heap_rebase(q, (uint16_t)(q->distance[cell] + cell_a(cell)));
	heap_put(q, cell_key(q, cell));
}

// Takes the first cell not settled and settles it, or BOT_NO_CELL.
static uint16_t SEC_BOTS heap_take(BotSearch *q)
{
	uint32_t *heap = q->heap, tail, v, v2;
	uint16_t n;
	int16_t end, at, child;

	while (q->count) {
		n = (uint16_t)(heap[0] & (BOT_CELLS - 1));
		end = (int16_t)(--q->count << 2);
		tail = HEAP_AT(heap, end);
		at = 0;
		while ((child = (int16_t)(at * 2 + 4)) < end) {
			v = HEAP_AT(heap, child);
			if (child + 4 < end) {
				v2 = HEAP_AT(heap, child + 4);
				if (v2 < v) {
					child += 4;
					v = v2;
				}
			}
			if (v > tail)
				break;
			HEAP_AT(heap, at) = v;
			at = child;
		}
		HEAP_AT(heap, at) = tail;
		if (!is_settled(q, n)) {
			q->settled[n >> 3] |= (uint8_t)(1 << (n & 7));
			return n;
		}
	}
	return BOT_NO_CELL;
}

static void SEC_BOTS heap_up(BotSearch *q, uint16_t n)
{
	tables(q);
	heap_push(q, n);
}

// For the host test, which takes cells without a search step.
static uint16_t SEC_BOTS __attribute__((unused)) heap_pop(BotSearch *q)
{
	tables(q);
	return heap_take(q);
}

static void SEC_BOTS search_begin(GameState *s, uint8_t t)
{
	BotSearch *q = &s->search;
	Bot *b = &s->bots[t];
	const Tank *k = &s->tanks[t];
	uint8_t tx = (uint8_t)(k->x >> 8), ty = (uint8_t)(k->y >> 8), i, *c;
	uint16_t j, start;

	q->who = (uint8_t)(t + 1);
	q->phase = 2;
	q->zone = 0;
	q->goalX = b->goalX;
	q->goalY = b->goalY;
	q->boat = k->boat;
	q->ox = window_origin(tx, b->goalX);
	q->oy = window_origin(ty, b->goalY);
	if (!b->fails && b->pathX == b->goalX && b->pathY == b->goalY &&
	    (uint8_t)(tx - b->pathOX) < BOT_WIN && (uint8_t)(ty - b->pathOY) < BOT_WIN &&
	    (uint8_t)(b->goalX - b->pathOX) < BOT_WIN && (uint8_t)(b->goalY - b->pathOY) < BOT_WIN) {
		q->ox = b->pathOX;
		q->oy = b->pathOY;
	}
	// Four bytes a pass, a quarter of the loop overhead on a 68k.
	for (j = 0, c = q->cell; j < sizeof(q->cell) / 4; j++, c += 4) {
		c[0] = 0;
		c[1] = 0;
		c[2] = 0;
		c[3] = 0;
	}
	for (j = 0; j < sizeof(q->danger); j++) {
		q->danger[j] = 0;
		q->settled[j] = 0;
	}
	{
		uint16_t *dp = q->distance;

		for (j = 0; j < BOT_CELLS / 4; j++, dp += 4)
			dp[0] = dp[1] = dp[2] = dp[3] = BOT_NO_CELL;
	}
	q->launch = BOT_NO_CELL;
	q->launchScore = BOT_NO_CELL;
	q->launchKind = 0;
	// Live pills, and bases it can't drive over, are solid.
	for (i = 0; i < s->map.numPills; i++) {
		const MapPill *p = &s->map.pills[i];

		if (p->active && !p->inTank && p->armour > 0 && (uint8_t)(p->x - q->ox) < BOT_WIN &&
		    (uint8_t)(p->y - q->oy) < BOT_WIN)
			set_nib(q, (uint16_t)((p->x - q->ox) + (p->y - q->oy) * BOT_WIN), 15);
	}
	for (i = 0; i < s->map.numBases; i++) {
		const MapBase *mb = &s->map.bases[i];

		if (mb->active && bases_cant_drive(s, i, k->player) && (uint8_t)(mb->x - q->ox) < BOT_WIN &&
		    (uint8_t)(mb->y - q->oy) < BOT_WIN)
			set_nib(q, (uint16_t)((mb->x - q->ox) + (mb->y - q->oy) * BOT_WIN), 15);
	}
	start = (uint16_t)((tx - q->ox) + (ty - q->oy) * BOT_WIN);
	set_nib(q, start, 9);
	// Something to shoot needs only to be got near.
	q->near = 0;
	if ((b->goal == BG_PILL && s->map.pills[b->target].armour > 0 &&
	     b->goalX == s->map.pills[b->target].x && b->goalY == s->map.pills[b->target].y) ||
	    (b->goal == BG_BASE && bases_cant_drive(s, b->target, k->player)))
		q->near = 3;
	else if (b->goal == BG_TANK)
		q->near = 2;
	q->count = 0;
	q->fbase = 0;
	q->distance[start] = 0;
	heap_up(q, start);
	q->best = start;
	q->bestH = q->startH = cell_h(q, start);
	q->cur = 0;
	b->want = 0;
}

static inline uint8_t SEC_BOTS water(uint8_t t)
{
	return t == TERRAIN_RIVER || t == TERRAIN_DEEP_SEA || t == TERRAIN_BOAT;
}

// Land after a boat is crossed on foot. A landing needs somewhere safe to go.
static uint8_t SEC_BOTS step_cost(const GameState *s, uint8_t x, uint8_t y, uint8_t afloat, uint8_t escape)
{
	uint8_t c, d, t = terrain(x, y), hidden = s->hiddenMines;

	if (t == TERRAIN_BOAT)
		return 1;
	c = cost_at(hidden, x, y, afloat);
	// A tank already beside deep water may follow shallow water back to land.
	if (!c && escape && t == TERRAIN_RIVER && x > MAP_EDGE_LOW + 1 && x < MAP_EDGE_HIGH - 1 &&
	    y > MAP_EDGE_LOW + 1 && y < MAP_EDGE_HIGH - 1)
		c = 6;
	if (!afloat || !c || water(t))
		return c;
	for (d = 0; d < 8; d += 2)
		if (cost_at(hidden, (uint8_t)(x + kDX[d]), (uint8_t)(y + kDY[d]), 0))
			return c;
	return 0;
}

// A boat at x, y to board (kind 1), or river to launch one on (kind 2),
// which needs no base or pill on it.
static void SEC_BOTS launch_candidate(GameState *s, uint16_t n, uint8_t kind, uint8_t x, uint8_t y)
{
	BotSearch *q = &s->search;
	uint16_t score;

	if (kind == 2 && (g_base_at(s, x, y) >= 0 || g_pill_at(s, x, y) >= 0))
		return;
	score = (uint16_t)(q->distance[n] + 2 * cheb(x, y, q->goalX, q->goalY) +
		(kind == 2 ? R_LGM_COST_BOAT : 0));
	if (score < q->launchScore) {
		q->launchScore = score;
		q->launch = n;
		q->launchKind = kind;
		q->launchX = x;
		q->launchY = y;
	}
}

// Window cells from a cell to its neighbour in each direction. Not const,
// so a 68k reads it off a5 rather than through the code segment.
static int8_t kStep[8] = { -BOT_WIN, 1 - BOT_WIN, 1, 1 + BOT_WIN, BOT_WIN, BOT_WIN - 1, -1, -1 - BOT_WIN };

// As cost_in for a tank with no boat, given the rows from the square's own
// on and its terrain t.
static inline uint8_t SEC_BOTS cost_land(uint8_t hidden, const uint8_t *const *rows, uint8_t x, uint8_t y,
	uint8_t t)
{
	const uint8_t *row = rows[0];

	if (t >= TERRAIN_MINE_FIRST && t <= TERRAIN_MINE_LAST) {
		if (!hidden || mapstore_flag(MS_KNOWN, x, y))
			return MINE_COST;
		t = (uint8_t)(t - TERRAIN_MINE_ADD);
	}
	if (is_wall(t) || t == TERRAIN_DEEP_SEA || row[x - 1] == TERRAIN_DEEP_SEA ||
	    row[x + 1] == TERRAIN_DEEP_SEA || rows[-1][x] == TERRAIN_DEEP_SEA || rows[1][x] == TERRAIN_DEEP_SEA)
		return 0;
	return t < 10 ? kLandCost[t] : 1;
}

// Marks direction bit b needed if the neighbour step cells on is in the
// window, not solid and dearer than the cheapest step would make it, and
// zone too if that step is into a pill's reach, which costs ZONE_WAIT more.
// Only a cell not reached yet can be solid.
#define LOOK(b, step) \
	do { \
		if (!(shut & (b)) && (o = dn[step]) > least) { \
			n = (uint16_t)(idx + (step)); \
			if (o == BOT_NO_CELL && nib(q, n) == 15) \
				shut |= (b); \
			else if (!(danger[n >> 3] & (1 << (n & 7)))) \
				need |= (b); \
			else if (o > (uint16_t)(least + ZONE_WAIT)) { \
				need |= (b); \
				zone |= (b); \
			} \
		} \
	} while (0)

// Expand only settled cells; stop at the goal when its cheapest cost is known.
// First it picks the neighbours a step could improve (need): in the window,
// not solid, and dearer now than the cheapest step would make them. A
// settled neighbour never is: cells settle in order of f, which a step
// changes by 1 at most, so it cost no more than this cell and 1. Then it
// works out the step cost of those and of the straight neighbours beside a
// diagonal one, straight ones first, and last takes the cheaper arrivals.
// Bits in cand, need, shut, zone and ok are 1 << direction. Needs tables().
static uint8_t SEC_BOTS expand(GameState *s, uint16_t idx)
{
	BotSearch *q = &s->search;
	uint16_t *dn = &q->distance[idx];
	uint8_t boat = q->boat;
	uint8_t lx = (uint8_t)(idx & (BOT_WIN - 1)), ly = (uint8_t)(idx >> 5);
	uint8_t x = (uint8_t)(q->ox + lx), y = (uint8_t)(q->oy + ly);
	uint8_t h = gRawX[lx] > gRawY[ly] ? gRawX[lx] : gRawY[ly];
	uint8_t here, afloat, landed, inner, escape = 2, d, bit;
	uint8_t need = 0, shut = 0, zone = 0, ok = 0, want, c[8];
	uint16_t least;
	const uint8_t *const *rows = &mapstore_rows[y];

	if (gFar && lx != gEdgeX && ly != gEdgeY)
		h = (uint8_t)(h > 255 - 32 ? 255 : h + 32);
	if (h <= q->near) {
		q->best = idx;
		q->bestH = h;
		return 1;
	}
	// Every neighbour is far enough inside the map for cost_in.
	inner = (uint8_t)(x - (MAP_EDGE_LOW + 3)) <= MAP_EDGE_HIGH - MAP_EDGE_LOW - 6 &&
		(uint8_t)(y - (MAP_EDGE_LOW + 3)) <= MAP_EDGE_HIGH - MAP_EDGE_LOW - 6;
	here = inner ? rows[0][x] : terrain(x, y);
	// A boat to board is a launch, so it doesn't count as how near land gets.
	if (!boat && here == TERRAIN_BOAT) {
		launch_candidate(s, idx, 1, x, y);
		return 0;
	}
	if (h < q->bestH || (h == q->bestH &&
	    (uint16_t)(gRawX[lx] + gRawY[ly]) <
	    (uint16_t)(gRawX[q->best & (BOT_WIN - 1)] + gRawY[q->best >> 5]))) {
		q->bestH = h;
		q->best = idx;
	}
	afloat = boat && water(here);
	landed = boat && !afloat;
	// From land, a river square beside it is somewhere to launch a boat.
	if (!boat && !water(here)) {
		if (inner) {
			if (rows[-1][x] == TERRAIN_RIVER)
				launch_candidate(s, idx, 2, x, (uint8_t)(y - 1));
			if (rows[0][x + 1] == TERRAIN_RIVER)
				launch_candidate(s, idx, 2, (uint8_t)(x + 1), y);
			if (rows[1][x] == TERRAIN_RIVER)
				launch_candidate(s, idx, 2, x, (uint8_t)(y + 1));
			if (rows[0][x - 1] == TERRAIN_RIVER)
				launch_candidate(s, idx, 2, (uint8_t)(x - 1), y);
		} else {
			for (d = 0; d < 8; d += 2) {
				uint8_t nx = (uint8_t)(x + kDX[d]), ny = (uint8_t)(y + kDY[d]);

				if (terrain(nx, ny) == TERRAIN_RIVER)
					launch_candidate(s, idx, 2, nx, ny);
			}
		}
	}
	// Neighbours out of the window: N NE NW, NE E SE, SE S SW, SW W NW.
	if (ly == 0)
		shut |= 0x83;
	if (lx == BOT_WIN - 1)
		shut |= 0x0E;
	if (ly == BOT_WIN - 1)
		shut |= 0x38;
	if (lx == 0)
		shut |= 0xE0;
	// A step costs 1 at least.
	least = (uint16_t)(dn[0] + 1);
	{
		const uint8_t *danger = q->danger;
		uint16_t o, n;

#ifdef EXPAND_ASM
		// Only cells near the map edge come here, so it is kept small.
		for (d = 0, bit = 1; d < 8; d++, bit = (uint8_t)(bit << 1)) {
			int16_t st = kStep[d];

			LOOK(bit, st);
		}
#else
		LOOK(0x01, -BOT_WIN);
		LOOK(0x02, 1 - BOT_WIN);
		LOOK(0x04, 1);
		LOOK(0x08, 1 + BOT_WIN);
		LOOK(0x10, BOT_WIN);
		LOOK(0x20, BOT_WIN - 1);
		LOOK(0x40, -1);
		LOOK(0x80, -1 - BOT_WIN);
#endif
	}
	if (!need)
		return 0;
	// The diagonals wanted, moved a direction each way, are their sides.
	want = (uint8_t)(need & 0xAA);
	want = (uint8_t)((need | (want << 1) | (want >> 1) | (want >> 7)) & ~shut);
	// Straight directions first, 0 2 4 6, then 1 3 5 7, as a diagonal
	// needs both sides open.
	for (d = 0, bit = 1; want; d = (uint8_t)(d == 6 ? 1 : d + 2), bit = (uint8_t)(d == 1 ? 2 : bit << 2)) {
		uint8_t nx, ny, nt, v;

		if (!(want & bit))
			continue;
		want &= (uint8_t)~bit;
		if (d & 1) {
			uint8_t sides = (uint8_t)((bit >> 1) | (bit << 1) | (bit >> 7));

			if ((ok & sides) != sides)
				continue;
		}
		nx = (uint8_t)(x + kDX[d]);
		ny = (uint8_t)(y + kDY[d]);
		if (inner && !landed) {
			const uint8_t *const *nr = rows + kDY[d];

			nt = nr[0][nx];
			if (boat) {
				// afloat. As step_cost for a boat that stays on the water.
				if (water(nt))
					v = 1;
				else
					v = step_cost(s, nx, ny, 1, 0);
			} else if (nt == TERRAIN_BOAT) {
				v = 1;
			} else {
				// As step_cost for a tank with no boat. The shoreline escape
				// is rare, so whether this cell allows it waits until asked.
				v = cost_land(s->hiddenMines, nr, nx, ny, nt);
				if (!v && nt == TERRAIN_RIVER) {
					if (escape == 2)
						escape = !cost(s, x, y, 0);
					if (escape)
						v = step_cost(s, nx, ny, 0, 1);
				}
			}
		} else {
			// A boat that has landed stays off the water.
			if (landed && water(terrain(nx, ny)))
				continue;
			if (escape == 2)
				escape = !boat && !cost(s, x, y, 0);
			v = step_cost(s, nx, ny, afloat, escape);
		}
		if (v) {
			c[d] = v;
			ok |= bit;
		}
	}
	need &= ok;
	for (d = 0, bit = 1; need; d++, bit = (uint8_t)(bit << 1)) {
		uint16_t distance;
		int16_t st;

		if (!(need & bit))
			continue;
		need &= (uint8_t)~bit;
		st = kStep[d];
		distance = (uint16_t)(least - 1 + c[d] + ((zone & bit) ? ZONE_WAIT : 0));
		if (distance < dn[st]) {
			uint16_t n = (uint16_t)(idx + st);

			dn[st] = distance;
			set_nib(q, n, (uint8_t)(d + 1));
			heap_push(q, n);
		}
	}
	return 0;
}
#undef LOOK

// ---- expand in 68k assembly (expand68k.S) ----
// expand68k.S does a cell well inside the map and hands the rest to
// expand_c. These are the C parts it calls.
#ifdef EXPAND_ASM
#include "expand68k.h"

#define EX_AT(t, f) ((unsigned long)&((t *)0)->f)
typedef char ex_offsets_match[EX_AT(GameState, search) == EX_SEARCH &&
	EX_AT(GameState, hiddenMines) == EX_HIDDEN && EX_AT(BotSearch, ox) == EX_OX &&
	EX_AT(BotSearch, oy) == EX_OY && EX_AT(BotSearch, boat) == EX_BOAT &&
	EX_AT(BotSearch, bestH) == EX_BESTH && EX_AT(BotSearch, near) == EX_NEAR &&
	EX_AT(BotSearch, best) == EX_BEST && EX_AT(BotSearch, count) == EX_COUNT &&
	EX_AT(BotSearch, fbase) == EX_FBASE && EX_AT(BotSearch, cell) == EX_CELL &&
	EX_AT(BotSearch, danger) == EX_DANGER && EX_AT(BotSearch, distance) == EX_DISTANCE &&
	EX_AT(BotSearch, heap) == EX_HEAP ? 1 : -1];

uint8_t SEC_BOTS expand_asm(GameState *s, uint16_t idx);
uint8_t SEC_BOTS expand_c(GameState *s, uint16_t idx);
void SEC_BOTS expand_launch(GameState *s, uint16_t n, uint8_t kind, uint8_t x, uint8_t y);
uint8_t SEC_BOTS expand_open(const GameState *s, uint8_t x, uint8_t y);
uint8_t SEC_BOTS expand_escape(const GameState *s, uint8_t x, uint8_t y);
uint8_t SEC_BOTS expand_landing(const GameState *s, uint8_t x, uint8_t y);
uint8_t SEC_BOTS expand_mine(const GameState *s, uint8_t x, uint8_t y, uint8_t t);
void SEC_BOTS expand_push(BotSearch *q, uint16_t cell);

uint8_t SEC_BOTS expand_c(GameState *s, uint16_t idx)
{
#ifdef SR_NOW
	uint16_t t0 = SR_NOW();
	uint8_t r = expand(s, idx);

	SR_ADD(gSearchSlow, t0);
	gSearchSlowN++;
	return r;
#else
	return expand(s, idx);
#endif
}

void SEC_BOTS expand_launch(GameState *s, uint16_t n, uint8_t kind, uint8_t x, uint8_t y)
{
#ifdef SR_NOW
	uint16_t t0 = SR_NOW();

	launch_candidate(s, n, kind, x, y);
	SR_ADD(gSearchLaunch, t0);
	gSearchLaunchN++;
#else
	launch_candidate(s, n, kind, x, y);
#endif
}

// Whether a tank can stand at x, y: if not it may follow river to land.
uint8_t SEC_BOTS expand_open(const GameState *s, uint8_t x, uint8_t y)
{
	return cost(s, x, y, 0);
}

uint8_t SEC_BOTS expand_escape(const GameState *s, uint8_t x, uint8_t y)
{
	return step_cost(s, x, y, 0, 1);
}

uint8_t SEC_BOTS expand_landing(const GameState *s, uint8_t x, uint8_t y)
{
	return step_cost(s, x, y, 1, 0);
}

uint8_t SEC_BOTS expand_mine(const GameState *s, uint8_t x, uint8_t y, uint8_t t)
{
	return cost_land(s->hiddenMines, &mapstore_rows[y], x, y, t);
}

void SEC_BOTS expand_push(BotSearch *q, uint16_t cell)
{
	heap_push(q, cell);
}

#ifdef EXPAND_CHECK
// Debug: runs expand68k.S and the C from the same search state and counts
// the cells where the result or the search state after differs. The C's
// result is the one kept. search_check_copy (timing.c) copies the state to
// copy 0 or 1, or back from one, and gives the copy, NULL with no memory.
extern BotSearch *search_check_copy(BotSearch *q, uint8_t which, uint8_t back)
	__attribute__((section("ui")));
unsigned long gExpandChecked, gExpandDiffer;
uint16_t gExpandFirst = BOT_NO_CELL;

static uint8_t SEC_BOTS expand_check(GameState *s, uint16_t idx)
{
	BotSearch *q = &s->search, *b;
	const uint16_t *x, *y;
	uint16_t i;
	uint8_t ra, rc, same;

	if (!search_check_copy(q, 0, 0))
		return expand(s, idx);
	ra = expand_asm(s, idx);
	b = search_check_copy(q, 1, 0);
	search_check_copy(q, 0, 1);
	rc = expand(s, idx);
	if (!b)
		return rc;
	same = ra == rc;
	for (i = 0, x = (const uint16_t *)q, y = (const uint16_t *)b; same && i < sizeof(BotSearch) / 2; i++)
		same = x[i] == y[i];
	gExpandChecked++;
	if (!same) {
		if (!gExpandDiffer)
			gExpandFirst = idx;
		gExpandDiffer++;
	}
	return rc;
}
#define EXPAND(s, n) expand_check(s, n)
#else
#define EXPAND(s, n) expand_asm(s, n)
#endif
#else
#define EXPAND(s, n) expand(s, n)
#endif

// Picks where the path ends and starts tracing it back. A launch is taken
// only when land stops more than a square short of what the goal needs and
// the launch's water is nearer the goal than land gets, so each boat trip
// ends nearer and a goal with no way on runs out of tries. A goal out of
// the window also needs land to have got no nearer at all.
static void SEC_BOTS trace_begin(BotSearch *q)
{
	if (!q->boat && q->launch != BOT_NO_CELL && q->bestH > (uint8_t)(q->near + 1) &&
	    cheb(q->launchX, q->launchY, q->goalX, q->goalY) <
	    cheb((uint8_t)(q->ox + (q->best & (BOT_WIN - 1))), (uint8_t)(q->oy + (q->best >> 5)),
	         q->goalX, q->goalY) &&
	    (((uint8_t)(q->goalX - q->ox) < BOT_WIN && (uint8_t)(q->goalY - q->oy) < BOT_WIN) ||
	     q->bestH >= q->startH))
		q->best = q->launch;
	else
		q->launchKind = 0;
	q->phase = 1;
	q->cur = q->best;
	q->wayN = 0;
	q->wayPos = 0;
	q->lastDir = 255;
}

// The path is done: its turning points nearest the tank go to the bot.
static void SEC_BOTS search_end(GameState *s)
{
	BotSearch *q = &s->search;
	uint8_t t = (uint8_t)(q->who - 1), i;
	Bot *b = &s->bots[t];
	uint8_t startH = cell_h(q, q->cur);     // the trace ends at the start

	q->who = 0;
	if (b->goalX != q->goalX || b->goalY != q->goalY) {
		b->wayN = 0;
		b->want = 1;
		return;
	}
	for (i = 0; i < q->wayN; i++) {
		uint8_t r = (uint8_t)((q->wayPos + BOT_WAYS - 1 - i) % BOT_WAYS);

		b->way[i][0] = q->way[r][0];
		b->way[i][1] = q->way[r][1];
	}
	b->wayN = q->wayN;
	b->pathX = q->goalX;
	b->pathY = q->goalY;
	b->pathOX = q->ox;
	b->pathOY = q->oy;
	b->boat = q->boat;
	// A full waypoint ring can omit the launch. Keep routing until it fits.
	b->boatTask = q->launchKind;
	if (b->boatTask && b->wayN &&
	    (b->way[b->wayN - 1][0] != q->ox + (q->launch & (BOT_WIN - 1)) ||
	     b->way[b->wayN - 1][1] != q->oy + (q->launch >> 5)))
		b->boatTask = 0;
	b->boatX = q->launchX;
	b->boatY = q->launchY;
	if (q->bestH >= startH && !q->launchKind)
		b->fails++;
	else
		b->fails = 0;
}

// Runs the search on by budget, in quarters: a cell expanded costs 5, an
// trace step 1, a pill zone 16. Heap operations take at most ten levels.
static void SEC_BOTS search_step(GameState *s, uint8_t budget)
{
	BotSearch *q = &s->search;
	uint16_t units = (uint16_t)budget * 4;

	if (!q->who)
		return;
	tables(q);
	while (units && q->who) {
		if (q->phase == 2) {
			// A pill out of the window costs nothing.
			if (q->zone >= s->map.numPills)
				q->phase = 0;
			else if (mark_zone(s, q->zone++))
				units = (uint16_t)(units > 16 ? units - 16 : 0);
			continue;
		}
		if (q->phase == 0) {
#ifdef SR_NOW
			uint16_t t0 = SR_NOW(), n = heap_take(q);
			uint8_t e;

			SR_ADD(gSearchTake, t0);
#else
			uint16_t n = heap_take(q);
#endif

			if (n == BOT_NO_CELL) {
				trace_begin(q);
				continue;
			}
			units = (uint16_t)(units > 5 ? units - 5 : 0);
			SEARCH_CELL();
#ifdef SR_NOW
			t0 = SR_NOW();
			e = EXPAND(s, n);
			SR_ADD(gSearchExpand, t0);
			if (e || ++q->cur >= BOT_MAX_CELLS)
#else
			if (EXPAND(s, n) || ++q->cur >= BOT_MAX_CELLS)
#endif
				trace_begin(q);
			continue;
		}
		for (; units; units--) {
			uint8_t v = nib(q, q->cur), d;

			if (v == 0 || v > 8) {
				search_end(s);
				return;
			}
			d = (uint8_t)(v - 1);
			if (d != q->lastDir) {
				q->way[q->wayPos][0] = (uint8_t)(q->ox + (q->cur & (BOT_WIN - 1)));
				q->way[q->wayPos][1] = (uint8_t)(q->oy + (q->cur >> 5));
				q->wayPos = (uint8_t)((q->wayPos + 1) % BOT_WAYS);
				if (q->wayN < BOT_WAYS)
					q->wayN++;
			}
			q->lastDir = d;
			q->cur = (uint16_t)(q->cur - kDX[d] - kDY[d] * BOT_WIN);
		}
	}
}

// ---- looking round ----

static uint8_t SEC_BOTS rnd(GameState *s, uint16_t n)
{
	return (uint8_t)game_random_below(s, n);
}

static void SEC_BOTS roll_aim(GameState *s, Bot *b)
{
	uint8_t e = kLevel[b->level].aimErr;

	b->aimErr = (uint8_t)(int8_t)((int16_t)rnd(s, (uint16_t)(e * 2 + 1)) - e);
}

// Ready for a run at a pill: armour and shells to spare.
static uint8_t SEC_BOTS run_ready(const Tank *k, const Bot *b)
{
	return k->armour >= kLevel[b->level].retreat + 10 && k->shells >= 8;
}

static void SEC_BOTS set_goal(Bot *b, uint8_t goal, uint8_t target, uint8_t x, uint8_t y)
{
	if (b->goal == goal && b->target == target &&
	    cheb(x, y, b->goalX, b->goalY) <= (goal == BG_TANK ? 3 : goal == BG_MAN ? 1 : 0))
		return;
	if (goal != b->goal || target != b->target) {
		b->mode = BM_NAV;
		b->fails = 0;
	}
	b->boatTask = 0;
	b->goal = goal;
	b->target = target;
	b->goalX = x;
	b->goalY = y;
	b->wayN = 0;
	b->want = 1;
}

// A hostile tank it can see within range squares, the nearest, or NO_TANK.
// One in forest further than 3 squares is hidden.
static uint8_t SEC_BOTS find_foe(const GameState *s, uint8_t t, uint8_t range)
{
	const Tank *k = &s->tanks[t];
	uint8_t i, best = NO_TANK, bd = 255, tx = (uint8_t)(k->x >> 8), ty = (uint8_t)(k->y >> 8);

	for (i = 0; i < s->numTanks; i++) {
		const Tank *o = &s->tanks[i];
		uint8_t ox = (uint8_t)(o->x >> 8), oy = (uint8_t)(o->y >> 8), d;

		if (i == t || o->destroyed || o->deathWait || allied(s, o->player, k->player))
			continue;
		d = cheb(tx, ty, ox, oy);
		if (d > range || d >= bd)
			continue;
		if (d > 3 && plain(terrain(ox, oy)) == TERRAIN_FOREST)
			continue;
		bd = d;
		best = i;
	}
	return best;
}

// How hard the live hostile pills in reach of square x, y fire on a tank
// there, as a percentage of the armour a base gives in the same time: 0 for
// none, 46 for one at rest, more when they are angry.
static uint16_t SEC_BOTS pill_heat(const GameState *s, uint8_t player, uint8_t x, uint8_t y)
{
	uint16_t heat = 0;
	uint8_t i;

	for (i = 0; i < s->map.numPills; i++) {
		const MapPill *p = &s->map.pills[i];
		uint8_t dx, dy;

		if (!pill_hostile(s, p, player) || p->armour == 0)
			continue;
		dx = udiff8(x, p->x);
		dy = udiff8(y, p->y);
		// 8 squares, and a bit for where the tank is on its square.
		if (dx > 8 || dy > 8 || dx * dx + dy * dy > 65)
			continue;
		heat = (uint16_t)(heat + (uint16_t)(R_BASE_REFUEL_ARMOUR * 100U) /
			(p->speed < R_PILL_ATTACK_MIN ? R_PILL_ATTACK_MIN : p->speed));
	}
	return heat;
}

#define HEAT_OK   60    // most heat a base is still worth sitting on at: one pill at rest
#define HOT_STOCK 60    // armour a base under fire must hold to be worth driving to for it

// Whether base i with pills firing on it is worth being on, or driving to
// when near is 0. Each hit also knocks the tank off the middle, so even one
// pill at rest takes nearly all the armour the base gives. On it the tank
// stays for armour while the base has some and for shells while it has
// armour to get out with, and a tank a hit short of full counts as full. It
// drives there only for a good stock of armour or, with armour to spare,
// for shells, so it can't go straight back to a base it left.
static uint8_t SEC_BOTS hot_ok(const GameState *s, uint8_t t, uint8_t i, uint16_t heat, uint8_t near)
{
	const Tank *k = &s->tanks[t];
	const MapBase *mb = &s->map.bases[i];
	uint8_t armour = k->armour < R_TANK_FULL_ARMOUR - R_SHELL_DAMAGE &&
		mb->armour >= R_BASE_MIN_ARMOUR + R_BASE_ARMOUR_GIVE;
	uint8_t shells = k->shells < R_TANK_FULL_SHELLS - 4 && mb->shells > 0;

	if (heat > HEAT_OK)
		return 0;
	if (near)
		return (armour && k->armour >= 10) || (shells && k->armour >= 20);
	return (armour && k->armour >= 20 && mb->armour >= HOT_STOCK) || (shells && k->armour >= 30);
}

// The nearest base of its side that can give something, pills or not, or -1.
static int8_t SEC_BOTS own_base(const GameState *s, uint8_t t)
{
	const Tank *k = &s->tanks[t];
	uint8_t i, bd = 255, tx = (uint8_t)(k->x >> 8), ty = (uint8_t)(k->y >> 8);
	int8_t best = -1;

	for (i = 0; i < s->map.numBases; i++) {
		const MapBase *mb = &s->map.bases[i];
		uint8_t d;

		if (!mb->active || mb->owner == MAP_NEUTRAL || !allied(s, mb->owner, k->player))
			continue;
		if (mb->armour < R_BASE_MIN_ARMOUR + R_BASE_ARMOUR_GIVE && mb->shells == 0)
			continue;
		d = cheb(tx, ty, mb->x, mb->y);
		if (d < bd) {
			bd = d;
			best = (int8_t)i;
		}
	}
	return best;
}

// The base to fill up at now: the nearest of its side that can give
// something and no live hostile pill reaches, else the nearest under fire
// that is still worth it, or -1.
static int8_t SEC_BOTS fuel_base(const GameState *s, uint8_t t)
{
	const Tank *k = &s->tanks[t];
	uint8_t i, bd = 255, hd = 255, tx = (uint8_t)(k->x >> 8), ty = (uint8_t)(k->y >> 8);
	int8_t best = -1, hot = -1;

	for (i = 0; i < s->map.numBases; i++) {
		const MapBase *mb = &s->map.bases[i];
		uint16_t heat;
		uint8_t d;

		if (!mb->active || mb->owner == MAP_NEUTRAL || !allied(s, mb->owner, k->player))
			continue;
		if (mb->armour < R_BASE_MIN_ARMOUR + R_BASE_ARMOUR_GIVE && mb->shells == 0)
			continue;
		d = cheb(tx, ty, mb->x, mb->y);
		if (d >= bd)
			continue;
		heat = pill_heat(s, k->player, mb->x, mb->y);
		if (!heat) {
			bd = d;
			best = (int8_t)i;
		} else if (d < hd && hot_ok(s, t, i, heat, d <= 1)) {
			hd = d;
			hot = (int8_t)i;
		}
	}
	return best >= 0 ? best : hot;
}

// Too worn for a pill run with no base worth going to. It then takes what
// it can elsewhere, else runs at a pill with the shells it has, else goes
// to a base under fire for some.
static uint8_t SEC_BOTS cornered(const GameState *s, uint8_t t)
{
	return !run_ready(&s->tanks[t], &s->bots[t]) && fuel_base(s, t) < 0;
}

// Whether there is a base or dead pill it could take as it is.
static uint8_t SEC_BOTS elsewhere(const GameState *s, uint8_t t)
{
	const Tank *k = &s->tanks[t];
	const Bot *b = &s->bots[t];
	uint8_t i;

	for (i = 0; i < s->map.numBases; i++) {
		const MapBase *mb = &s->map.bases[i];

		if (mb->active && !allied(s, mb->owner, k->player) && b->skip != ((BK_BASE << 5) | i) &&
		    (mb->owner == MAP_NEUTRAL || mb->armour <= R_BASE_CAPTURE_ARMOUR || k->shells >= 10))
			return 1;
	}
	for (i = 0; i < s->map.numPills; i++) {
		const MapPill *p = &s->map.pills[i];

		if (pill_hostile(s, p, k->player) && p->armour == 0 && b->skip != ((BK_PILL << 5) | i))
			return 1;
	}
	return 0;
}

// Ready for a run at a pill, or cornered with shells, when it runs anyway.
static uint8_t SEC_BOTS run_ok(const GameState *s, uint8_t t)
{
	const Tank *k = &s->tanks[t];

	return run_ready(k, &s->bots[t]) || (k->shells > 0 && fuel_base(s, t) < 0);
}

#define LAST_SHELLS 8   // cornered with fewer shells it goes to a base under fire first

// Whether a neutral base is less than half as far as base own and at
// least 8 squares nearer.
static uint8_t SEC_BOTS neutral_nearer(const GameState *s, uint8_t t, uint8_t own)
{
	const Tank *k = &s->tanks[t];
	uint8_t i, tx = (uint8_t)(k->x >> 8), ty = (uint8_t)(k->y >> 8);
	uint8_t od = cheb(tx, ty, s->map.bases[own].x, s->map.bases[own].y);

	for (i = 0; i < s->map.numBases; i++) {
		const MapBase *mb = &s->map.bases[i];
		uint8_t d = cheb(tx, ty, mb->x, mb->y);

		if (mb->active && mb->owner == MAP_NEUTRAL && d * 2 < od && d + 8 <= od)
			return 1;
	}
	return 0;
}

// The nearest live hostile pill within 8 squares of x, y, or -1.
static int8_t SEC_BOTS guard_pill(const GameState *s, uint8_t t, uint8_t x, uint8_t y)
{
	uint8_t i, bd = 255, player = s->tanks[t].player;
	int8_t best = -1;

	for (i = 0; i < s->map.numPills; i++) {
		const MapPill *p = &s->map.pills[i];
		uint8_t d;

		if (!pill_hostile(s, p, player) || p->armour == 0 || s->bots[t].skip == ((BK_PILL << 5) | i))
			continue;
		d = cheb(x, y, p->x, p->y);
		if (d <= 8 && d < bd) {
			bd = d;
			best = (int8_t)i;
		}
	}
	return best;
}

// Where the live hostile pills are, for guards.
typedef struct Hostile {
	uint8_t n, x[MAP_MAX_PILLS], y[MAP_MAX_PILLS];
} Hostile;

static void SEC_BOTS hostile_pills(const GameState *s, uint8_t player, Hostile *h)
{
	uint8_t i;

	h->n = 0;
	for (i = 0; i < s->map.numPills; i++) {
		const MapPill *p = &s->map.pills[i];

		if (p->armour > 0 && pill_hostile(s, p, player)) {
			h->x[h->n] = p->x;
			h->y[h->n] = p->y;
			h->n++;
		}
	}
}

// Live hostile pills within reach of x, y.
static uint8_t SEC_BOTS guards(const Hostile *h, uint8_t x, uint8_t y)
{
	uint8_t i, n = 0;

	// Within 8 squares on both axes, without the wrap of 8-bit sums.
	for (i = 0; i < h->n; i++)
		if ((uint16_t)(x - h->x[i] + 8) <= 16 && (uint16_t)(y - h->y[i] + 8) <= 16)
			n++;
	return n;
}

// Own or allied live pills within 6 squares of x, y.
static uint8_t SEC_BOTS own_guards(const GameState *s, uint8_t player, uint8_t x, uint8_t y)
{
	uint8_t i, n = 0;

	for (i = 0; i < s->map.numPills; i++) {
		const MapPill *p = &s->map.pills[i];

		if (p->active && !p->inTank && p->owner != MAP_NEUTRAL && allied(s, p->owner, player) &&
		    cheb(x, y, p->x, p->y) <= 6)
			n++;
	}
	return n;
}

// The own base to put a carried pill by: near, with few own pills round
// it. -1 if none.
static int8_t SEC_BOTS guard_base(const GameState *s, uint8_t t)
{
	const Tank *k = &s->tanks[t];
	uint8_t i, tx = (uint8_t)(k->x >> 8), ty = (uint8_t)(k->y >> 8);
	uint16_t bd = 0xFFFF, d;
	int8_t best = -1;

	for (i = 0; i < s->map.numBases; i++) {
		const MapBase *mb = &s->map.bases[i];

		if (!mb->active || mb->owner == MAP_NEUTRAL || !allied(s, mb->owner, k->player))
			continue;
		d = (uint16_t)(cheb(tx, ty, mb->x, mb->y) + own_guards(s, k->player, mb->x, mb->y) * 24);
		if (d < bd) {
			bd = d;
			best = (int8_t)i;
		}
	}
	return best;
}

// Whether the goal it has is still worth going on with.
static uint8_t SEC_BOTS goal_ok(const GameState *s, uint8_t t)
{
	const Tank *k = &s->tanks[t];
	const Bot *b = &s->bots[t];

	switch (b->goal) {
	case BG_BASE: {
		const MapBase *mb = &s->map.bases[b->target];

		if (!mb->active || allied(s, mb->owner, k->player))
			return 0;
		return mb->owner == MAP_NEUTRAL || mb->armour <= R_BASE_CAPTURE_ARMOUR || k->shells > 0;
	}
	case BG_PILL: {
		const MapPill *p = &s->map.pills[b->target];

		return pill_hostile(s, p, k->player) && (p->armour == 0 || k->shells > 0);
	}
	case BG_PATROL:
		return cheb((uint8_t)(k->x >> 8), (uint8_t)(k->y >> 8), b->goalX, b->goalY) > 1;
	}
	return 0;
}

// No wall or forest on the line from one point to another, ends left out,
// checked at most 16 times along it.
static uint8_t SEC_BOTS line_open(uint16_t x0, uint16_t y0, uint16_t x, uint16_t y)
{
	int16_t dx = (int16_t)(x - x0), dy = (int16_t)(y - y0);
	uint16_t n = (uint16_t)(wdist(x0, y0, x, y) >> 7), i;
	int32_t ax = x0, ay = y0, sx, sy;
	uint8_t ex = (uint8_t)(x >> 8), ey = (uint8_t)(y >> 8);
	Row1 rc;

	rc.r = 0;
	if (n == 0)
		return 1;
	if (n > 16)
		n = 16;
	sx = dx / (int16_t)n;
	sy = dy / (int16_t)n;
	for (i = 1; i < n; i++) {
		uint8_t mx, my, t;

		ax += sx;
		ay += sy;
		mx = (uint8_t)((uint16_t)ax >> 8);
		my = (uint8_t)((uint16_t)ay >> 8);
		if (mx == ex && my == ey)
			break;
		t = plain(terrain_c(&rc, mx, my));
		if (is_wall(t) || t == TERRAIN_FOREST)
			return 0;
	}
	return 1;
}

// The builder can walk the line from the tank to the middle of square x, y:
// no wall, water or live pill on it before that square.
static uint8_t SEC_BOTS man_line(const GameState *s, const Tank *k, uint8_t x, uint8_t y)
{
	uint16_t ex = mid(x), ey = mid(y);
	int16_t dx = (int16_t)(ex - k->x), dy = (int16_t)(ey - k->y);
	uint8_t n = (uint8_t)((wdist(k->x, k->y, ex, ey) >> 7) + 1), i;
	Row1 rc;

	rc.r = 0;
	for (i = 1; i < n; i++) {
		uint8_t mx = (uint8_t)((uint16_t)(k->x + (int16_t)((int32_t)dx * i / n)) >> 8);
		uint8_t my = (uint8_t)((uint16_t)(k->y + (int16_t)((int32_t)dy * i / n)) >> 8), t;

		if (mx == x && my == y)
			break;
		t = plain(terrain_c(&rc, mx, my));
		if (is_wall(t) || t == TERRAIN_RIVER || t == TERRAIN_DEEP_SEA || g_live_pill_at(s, mx, my) >= 0)
			return 0;
	}
	return 1;
}

// Pill stages, just out of a pill's reach of 8 squares, west and east of
// it, from level to 45 degrees north and south.
#define STAGES 10
static const int8_t kSX[STAGES] = { -9, 9, -8, 8, -8, 8, -6, 6, -6, 6 };
static const int8_t kSY[STAGES] = { 0, 0, -4, -4, 4, 4, -6, -6, 6, 6 };

// How good a run from square x, y at the pill at px, py would be: 0 if a
// wall is in the way, else 1 plus a penalty for each tree to shoot through
// and each square of the first half of the run that is slow to cross
// (river is fast on a boat).
static uint8_t SEC_BOTS run_line(Rows16 *rc, uint8_t x, uint8_t y, uint8_t px, uint8_t py, uint8_t boat)
{
	int16_t sx = (int16_t)(((int16_t)px - x) * 16), sy = (int16_t)(((int16_t)py - y) * 16);
	uint16_t ax = mid(x), ay = mid(y);
	uint8_t i, score = 1;

	for (i = 1; i < 16; i++) {
		uint8_t t;

		ax = (uint16_t)(ax + sx);
		ay = (uint16_t)(ay + sy);
		if ((ax >> 8) == px && (ay >> 8) == py)
			break;
		t = plain(terrain16(rc, (uint8_t)(ax >> 8), (uint8_t)(ay >> 8)));
		if (is_wall(t))
			return 0;
		if (t == TERRAIN_FOREST)
			score = (uint8_t)(score + 3);
		else if (i < 8 && t != TERRAIN_GRASS && t != TERRAIN_ROAD && !(boat && t == TERRAIN_RIVER))
			score = (uint8_t)(score + 4);
	}
	return score;
}

// Where to start runs at live pill p from: a stage it can wait on, with a
// run line to the pill and quick ground to it, the nearest to the tank
// (forest and slow ground count against it). Each failure there
// moves it on to the next. A pill aims as if a tank were driving north
// (see pills.c), so a fast tank crossing the north-south line is mostly
// missed and one driving along it is hit. 0 if there is no good stage.
static uint8_t SEC_BOTS pill_stage(const GameState *s, uint8_t t, uint8_t p, uint8_t *gx, uint8_t *gy)
{
	const Tank *k = &s->tanks[t];
	const MapPill *mp = &s->map.pills[p];
	uint8_t i, tx = (uint8_t)(k->x >> 8), ty = (uint8_t)(k->y >> 8);
	uint16_t bd = 0xFFFF, d;
	uint8_t skip = (uint8_t)(s->bots[t].fails % STAGES);
	Rows16 rc;

	// The stages and their run lines are within 7 rows of the pill.
	rc.y0 = (uint8_t)(mp->y - 8);
	for (i = 0; i < 16; i++)
		rc.r[i] = 0;
	for (i = 0; i < STAGES; i++) {
		uint8_t x = (uint8_t)(mp->x + kSX[i]), y = (uint8_t)(mp->y + kSY[i]), r, c;

		// Not at sea, or in a river off a boat, where waiting loses shells.
		c = cost(s, x, y, k->boat);
		r = plain(terrain16(&rc, x, y));
		if (c == 0 || c == MINE_COST || r == TERRAIN_DEEP_SEA || (!k->boat && r == TERRAIN_RIVER))
			continue;
		r = run_line(&rc, x, y, mp->x, mp->y, k->boat);
		if (r == 0 || r > 40)
			continue;
		d = (uint16_t)(cheb(tx, ty, x, y) + r * 2 + c * 4 + (skip && i == skip - 1 ? 40 : 0));
		if (d < bd) {
			bd = d;
			*gx = x;
			*gy = y;
		}
	}
	return bd != 0xFFFF;
}

// A square to wait on out of reach of every live hostile pill: the nearest
// stage it can stand on of a pill that is near the tank. 0 if none.
static uint8_t SEC_BOTS back_off(const GameState *s, uint8_t t, uint8_t *gx, uint8_t *gy)
{
	const Tank *k = &s->tanks[t];
	uint8_t i, j, bd = 255, tx = (uint8_t)(k->x >> 8), ty = (uint8_t)(k->y >> 8);

	for (i = 0; i < s->map.numPills; i++) {
		const MapPill *p = &s->map.pills[i];

		if (!pill_hostile(s, p, k->player) || p->armour == 0 || cheb(tx, ty, p->x, p->y) > 9)
			continue;
		for (j = 0; j < STAGES; j++) {
			uint8_t x = (uint8_t)(p->x + kSX[j]), y = (uint8_t)(p->y + kSY[j]), d = cheb(tx, ty, x, y), c, r;

			if (d >= bd)
				continue;
			c = cost(s, x, y, k->boat);
			r = plain(terrain(x, y));
			if (c == 0 || c == MINE_COST || r == TERRAIN_DEEP_SEA || (!k->boat && r == TERRAIN_RIVER))
				continue;
			if (pill_heat(s, k->player, x, y))
				continue;
			bd = d;
			*gx = x;
			*gy = y;
		}
	}
	return bd != 255;
}

// Whether a run heading aim would be near the north-south line, where the
// pill's aim finds it: within 30 degrees of it.
static uint8_t SEC_BOTS run_north(uint16_t aim)
{
	uint16_t a = (uint16_t)(aim & (ANGLE_MAX / 2 - 1));

	return a < ANGLE_MAX / 12 || a > ANGLE_MAX / 2 - ANGLE_MAX / 12;
}

// The nearest base or pill worth taking, scored by distance with a bonus
// for neutral bases and dead pills; if pills guard it those come first.
static void SEC_BOTS choose(GameState *s, uint8_t t)
{
	const Tank *k = &s->tanks[t];
	Bot *b = &s->bots[t];
	uint8_t i, tx = (uint8_t)(k->x >> 8), ty = (uint8_t)(k->y >> 8), kind = 0, idx = 0, gx = 0, gy = 0;
	int16_t best = 0x7FFF, sc;
	int8_t g;
	uint8_t worn = cornered(s, t);
	Hostile h;

	hostile_pills(s, k->player, &h);
	for (i = 0; i < s->map.numBases; i++) {
		const MapBase *mb = &s->map.bases[i];
		uint8_t hostile = mb->owner != MAP_NEUTRAL;

		if (!mb->active || allied(s, mb->owner, k->player) || b->skip == ((BK_BASE << 5) | i))
			continue;
		if (hostile && mb->armour > R_BASE_CAPTURE_ARMOUR && k->shells < 10)
			continue;
		sc = (int16_t)(cheb(tx, ty, mb->x, mb->y) * 4 + rnd(s, 8) + guards(&h, mb->x, mb->y) * 48);
		if (!hostile)
			sc = (int16_t)(sc - 16);
		else if (mb->armour > R_BASE_CAPTURE_ARMOUR)
			sc = (int16_t)(sc + mb->armour / 4);
		if (sc < best) {
			best = sc;
			kind = BK_BASE;
			idx = i;
			gx = mb->x;
			gy = mb->y;
		}
	}
	for (i = 0; i < s->map.numPills; i++) {
		const MapPill *p = &s->map.pills[i];

		if (!pill_hostile(s, p, k->player) || b->skip == ((BK_PILL << 5) | i))
			continue;
		// Cornered, a live pill will do with any shells, after all else.
		if (p->armour > 0 && k->shells < (worn ? 1 : 10))
			continue;
		sc = (int16_t)(cheb(tx, ty, p->x, p->y) * 4 + rnd(s, 8));
		sc = (int16_t)(p->armour == 0 ? sc - 24 : sc + 8 + p->armour * 2 + (worn ? 2000 : 0));
		if (sc < best) {
			best = sc;
			kind = BK_PILL;
			idx = i;
			gx = p->x;
			gy = p->y;
		}
	}
	// With no base yet, or cornered, it dashes for a neutral one, guarded
	// or not.
	if (kind && !(kind == BK_PILL && s->map.pills[idx].armour > 0) && k->shells >= 10 &&
	    !(kind == BK_BASE && s->map.bases[idx].owner == MAP_NEUTRAL && (worn || own_base(s, t) < 0))) {
		g = guard_pill(s, t, gx, gy);
		if (g >= 0) {
			kind = BK_PILL;
			idx = (uint8_t)g;
			gx = s->map.pills[idx].x;
			gy = s->map.pills[idx].y;
		}
	}
	if (kind) {
		if (kind == BK_PILL && s->map.pills[idx].armour > 0 && !pill_stage(s, t, idx, &gx, &gy)) {
			// No way to run at it: leave it a while.
			b->skip = (uint8_t)((BK_PILL << 5) | idx);
			b->skipTime = 250;
			b->goal = BG_NONE;
			return;
		}
		set_goal(b, kind == BK_BASE ? BG_BASE : BG_PILL, idx, gx, gy);
		return;
	}
	// Nothing to take: out of reach of any pill firing on it, then round
	// the bases. From a safe square it leaves a spot under fire alone.
	worn = pill_heat(s, k->player, tx, ty) != 0;
	if (worn && back_off(s, t, &gx, &gy)) {
		set_goal(b, BG_PATROL, NO_BASE, gx, gy);
		return;
	}
	if (s->map.numBases > 0) {
		i = rnd(s, s->map.numBases);
		if (s->map.bases[i].active) {
			gx = (uint8_t)(s->map.bases[i].x + rnd(s, 5) - 2);
			gy = (uint8_t)(s->map.bases[i].y + rnd(s, 5) - 2);
			if (worn || !pill_heat(s, k->player, gx, gy)) {
				set_goal(b, BG_PATROL, i, gx, gy);
				return;
			}
		}
	}
	b->goal = BG_NONE;
}

// Somewhere near x, y (2 or 3 squares off) on open land with nothing on
// it or around it, for a pill or a mine. 0 if the square picked isn't.
static uint8_t SEC_BOTS spot_near(GameState *s, uint8_t t, uint8_t x, uint8_t y, uint8_t *ox, uint8_t *oy)
{
	const Tank *k = &s->tanks[t];
	uint8_t sx = (uint8_t)(x + rnd(s, 7) - 3), sy = (uint8_t)(y + rnd(s, 7) - 3), ter, i;

	if (cheb(sx, sy, x, y) < 2 || g_is_mine(sx, sy) || g_pill_at(s, sx, sy) >= 0 ||
	    g_base_at(s, sx, sy) >= 0 || (sx == (k->x >> 8) && sy == (k->y >> 8)))
		return 0;
	ter = terrain(sx, sy);
	if (ter != TERRAIN_GRASS && ter != TERRAIN_ROAD && ter != TERRAIN_SWAMP && ter != TERRAIN_RUBBLE)
		return 0;
	// Only in the open, so it can't shut a way: no mine beside it and at
	// most one side blocked.
	for (i = 0, ter = 0; i < 8; i++) {
		uint8_t c = cost(s, (uint8_t)(sx + kDX[i]), (uint8_t)(sy + kDY[i]), 0);

		if (c == MINE_COST || (c == 0 && (i & 1) == 0 && ++ter > 1))
			return 0;
	}
	*ox = sx;
	*oy = sy;
	return 1;
}

// Builder orders when the man is in the tank: put a carried pill down by
// an own base, mend an own pill, cut trees, lay a mine by an own base.
static void SEC_BOTS use_builder(GameState *s, uint8_t t, GameInput *in)
{
	const Tank *k = &s->tanks[t];
	Bot *b = &s->bots[t];
	uint8_t tx = (uint8_t)(k->x >> 8), ty = (uint8_t)(k->y >> 8), i, x = 0, y = 0;
	int8_t home = fuel_base(s, t);
	uint8_t nearHome = 0;

	if (b->buildWait || !builder_free(s, t))
		return;
	if (b->boatTask)
		goto trees;
	for (i = 0; i < s->map.numBases; i++) {
		const MapBase *mb = &s->map.bases[i];

		if (mb->active && mb->owner != MAP_NEUTRAL && allied(s, mb->owner, k->player) &&
		    cheb(tx, ty, mb->x, mb->y) <= 5) {
			nearHome = 1;
			home = (int8_t)i;
			break;
		}
	}
	if (k->numCarry > 0 && k->trees >= R_LGM_COST_PILL_NEW) {
		int8_t g = guard_base(s, t);

		if (g >= 0 && cheb(tx, ty, s->map.bases[g].x, s->map.bases[g].y) <= 5 &&
		    spot_near(s, t, s->map.bases[g].x, s->map.bases[g].y, &x, &y)) {
			in->build = BUILD_PILL;
			goto order;
		}
	}
	for (i = 0; i < s->map.numPills; i++) {
		const MapPill *p = &s->map.pills[i];

		if (p->active && !p->inTank && p->owner != MAP_NEUTRAL && allied(s, p->owner, k->player) &&
		    p->armour <= R_PILL_MAX_ARMOUR - 5 && k->trees >= 2 && cheb(tx, ty, p->x, p->y) <= 6 &&
		    !(p->x == tx && p->y == ty)) {
			in->build = BUILD_PILL;
			x = p->x;
			y = p->y;
			goto order;
		}
	}
trees:
	if (k->trees < (b->boatTask == 2 ? R_LGM_COST_BOAT : 10)) {
		uint8_t r;

		for (r = 1; r <= 3; r++) {
			int8_t dx, dy;

			for (dy = (int8_t)-r; dy <= (int8_t)r; dy++) {
				for (dx = (int8_t)-r; dx <= (int8_t)r; dx++) {
					if (dx != -r && dx != r && dy != -r && dy != r)
						continue;
					x = (uint8_t)(tx + dx);
					y = (uint8_t)(ty + dy);
					if (terrain(x, y) == TERRAIN_FOREST && g_pill_at(s, x, y) < 0) {
						in->build = BUILD_TREE;
						goto order;
					}
				}
			}
		}
	}
	if (b->boatTask)
		return;
	if (nearHome && b->level >= BOT_NORMAL && b->minesLaid < 8 && k->mines >= 10 && rnd(s, 6) == 0 &&
	    spot_near(s, t, s->map.bases[home].x, s->map.bases[home].y, &x, &y)) {
		in->build = BUILD_MINE;
		b->minesLaid++;
		goto order;
	}
	return;
order:
	if (!man_line(s, k, x, y)) {
		in->build = BUILD_NONE;
		b->buildWait = 20;
		return;
	}
	in->buildX = x;
	in->buildY = y;
	b->buildWait = 100;
}

// The builder is on his way back but blocked, or far off: the tank goes to
// get him, unless it is about to take what it is after. Once it has set
// out it keeps on until he is in.
static uint8_t SEC_BOTS man_lost(const GameState *s, uint8_t t)
{
	const Builder *m = &s->men[t];
	const Tank *k = &s->tanks[t];
	const Bot *b = &s->bots[t];
	uint8_t tx = (uint8_t)(k->x >> 8), ty = (uint8_t)(k->y >> 8);

	if (m->inTank || m->isDead || m->state != LGM_STATE_RETURN)
		return 0;
	if (b->goal == BG_MAN)
		return 1;
	// A base or pill nearly reached comes first.
	if (!m->obstructed && (b->goal == BG_BASE || b->goal == BG_PILL || b->goal == BG_REFUEL) &&
	    cheb(tx, ty, b->goalX, b->goalY) <= 3)
		return 0;
	return m->obstructed || cheb(tx, ty, (uint8_t)(m->x >> 8), (uint8_t)(m->y >> 8)) > 6;
}

static void SEC_BOTS think(GameState *s, uint8_t t, GameInput *in)
{
	const Tank *k = &s->tanks[t];
	Bot *b = &s->bots[t];
	const Level *lv = &kLevel[b->level];
	uint8_t foe = find_foe(s, t, lv->engage);
	int8_t base;

	if (foe != b->foe) {
		b->foe = foe;
		if (foe != NO_TANK) {
			b->react = lv->react;
			roll_aim(s, b);
		}
	}
	// A run at a pill ends on its own.
	if (b->mode == BM_RUN || b->mode == BM_VEER)
		return;
	if (b->fails >= 3) {
		// Can't get there: leave it a while.
		if (b->goal == BG_BASE || b->goal == BG_PILL || b->goal == BG_TANK) {
			b->skip = (uint8_t)(((b->goal == BG_BASE ? BK_BASE : b->goal == BG_PILL ? BK_PILL : BK_TANK)
				<< 5) | b->target);
			b->skipTime = 250;
		}
		b->goal = BG_NONE;
		b->boatTask = 0;
		b->fails = 0;
	}
	if (b->goal == BG_REFUEL) {
		const MapBase *mb = &s->map.bases[b->target];
		uint8_t on = (k->x >> 8) == mb->x && (k->y >> 8) == mb->y;
		uint16_t heat = pill_heat(s, k->player, mb->x, mb->y);
		uint8_t dry = mb->armour < R_BASE_MIN_ARMOUR + R_BASE_ARMOUR_GIVE && mb->shells == 0;

		// With pills firing on the base it goes on only while that pays,
		// or it has nothing else it can do.
		if (mb->owner != MAP_NEUTRAL && allied(s, mb->owner, k->player) &&
		    (heat ? hot_ok(s, t, b->target, heat,
				cheb((uint8_t)(k->x >> 8), (uint8_t)(k->y >> 8), mb->x, mb->y) <= 1) ||
			    (!dry && k->shells < LAST_SHELLS && fuel_base(s, t) < 0 && !elsewhere(s, t)) :
		     (k->armour < R_TANK_FULL_ARMOUR || k->shells < R_TANK_FULL_SHELLS - 4) && !(on && dry)))
			return;
		b->goal = BG_NONE;
	}
	if (k->armour < lv->retreat || k->shells < 3 ||
	    (b->goal == BG_PILL && s->map.pills[b->target].armour > 0 && !run_ready(k, b))) {
		base = fuel_base(s, t);
		// A neutral base much nearer than its own fills it up too.
		if (base >= 0 && neutral_nearer(s, t, (uint8_t)base)) {
			if (b->goal == BG_BASE && s->map.bases[b->target].owner == MAP_NEUTRAL)
				return;
			base = -1;
		}
		if (base >= 0) {
			set_goal(b, BG_REFUEL, (uint8_t)base, s->map.bases[base].x, s->map.bases[base].y);
			return;
		}
	}
	if (man_lost(s, t)) {
		set_goal(b, BG_MAN, 0, (uint8_t)(s->men[t].x >> 8), (uint8_t)(s->men[t].y >> 8));
		return;
	}
	if (b->goal == BG_MAN)
		b->goal = BG_NONE;
	// Chases a foe it can drive to.
	if (foe != NO_TANK && lv->chase && !k->boat && k->shells >= 5 && b->skip != ((BK_TANK << 5) | foe) &&
	    cost(s, (uint8_t)(s->tanks[foe].x >> 8), (uint8_t)(s->tanks[foe].y >> 8), k->boat)) {
		set_goal(b, BG_TANK, foe, (uint8_t)(s->tanks[foe].x >> 8), (uint8_t)(s->tanks[foe].y >> 8));
		return;
	}
	if (b->goal == BG_TANK)
		b->goal = BG_NONE;
	if (foe == NO_TANK)
		use_builder(s, t, in);
	// A carried pill goes by an own base that has few.
	if (k->numCarry > 0 && k->trees >= R_LGM_COST_PILL_NEW && b->goal != BG_BASE) {
		base = guard_base(s, t);
		if (base >= 0) {
			const MapBase *mb = &s->map.bases[base];

			if (!(b->goal == BG_PATROL && b->target == (uint8_t)base) &&
			    cheb((uint8_t)(k->x >> 8), (uint8_t)(k->y >> 8), mb->x, mb->y) > 3)
				set_goal(b, BG_PATROL, (uint8_t)base, mb->x, mb->y);
			if (b->goal == BG_PATROL && b->target == (uint8_t)base)
				return;
		}
	}
	if (b->boatTask)
		return;
	// Cornered: anything it can take elsewhere comes before a live pill,
	// and a base under fire before a run with too few shells.
	base = -1;
	if (cornered(s, t)) {
		uint8_t other = elsewhere(s, t);

		if (!other && k->shells < LAST_SHELLS)
			base = own_base(s, t);
		if (b->goal == BG_PILL && s->map.pills[b->target].armour > 0 && (other || base >= 0))
			b->goal = BG_NONE;
	}
	if (goal_ok(s, t))
		return;
	if (base >= 0) {
		set_goal(b, BG_REFUEL, (uint8_t)base, s->map.bases[base].x, s->map.bases[base].y);
		return;
	}
	// Top up when there is nothing pressing.
	if (k->armour < R_TANK_FULL_ARMOUR - 10 || k->shells < R_TANK_FULL_SHELLS / 2) {
		base = fuel_base(s, t);
		if (base >= 0) {
			set_goal(b, BG_REFUEL, (uint8_t)base, s->map.bases[base].x, s->map.bases[base].y);
			return;
		}
	}
	choose(s, t);
}

// ---- driving ----

// Deep sea within stopping distance ahead.
static uint8_t SEC_BOTS sea_ahead(const Tank *k)
{
	int16_t dx, dy;
	uint16_t stop;
	uint16_t a = (uint16_t)(util_dir16(k->angle) * 16 * ANGLE_ONE);

	if (k->boat)
		return 0;
	stop = (uint16_t)(((uint16_t)k->speed * (uint16_t)k->speed >> 3) + 160);
	if (stop > 1023)
		stop = 1023;
	util_distance(&dx, &dy, a, stop);
	if (terrain((uint8_t)((uint16_t)(k->x + dx) >> 8), (uint8_t)((uint16_t)(k->y + dy) >> 8)) ==
	    TERRAIN_DEEP_SEA)
		return 1;
	util_distance(&dx, &dy, a, (uint16_t)(stop >> 1));
	return terrain((uint8_t)((uint16_t)(k->x + dx) >> 8), (uint8_t)((uint16_t)(k->y + dy) >> 8)) ==
		TERRAIN_DEEP_SEA;
}

// The keys for what steer last set, from the tank as it is now: turns
// towards the heading within the dead zone and makes for the speed,
// slowing for a sharp turn unless kept.
static void SEC_BOTS steer_keys(const Tank *k, const Bot *b, GameInput *in)
{
	int16_t d, ad, tsp = b->tsp, dead = b->dead;

	if (b->sightWant) {
		if (k->sightLen < b->sightWant)
			in->keys |= IN_SIGHT_FAR;
		else if (k->sightLen > b->sightWant)
			in->keys |= IN_SIGHT_NEAR;
	}
	if (!(b->steerBits & SB_ON))
		return;
	d = turn_to((uint16_t)(((uint16_t)b->wantHi << 8) | b->wantLo), k->angle);
	ad = d < 0 ? (int16_t)-d : d;
	if (d > dead)
		in->keys |= IN_RIGHT;
	else if (d < -dead)
		in->keys |= IN_LEFT;
	if (b->steerBits & SB_KEEP) {
		;
	} else if (!k->boat) {
		if (ad > 2048)
			tsp = 0;
		else if (ad > 1024)
			tsp = 0;
	} else if (ad > 2048 && tsp > 16) {
		tsp = 16;
	}
	if (b->steerBits & SB_SEA)
		tsp = 0;
	if (k->speed < tsp)
		in->keys |= IN_FASTER;
	else if (k->speed > (tsp ? tsp + 3 : 0))
		in->keys |= IN_SLOWER;
}

// Turns towards want (within dead, at most 255) and makes for speed tsp
// (quarters, 0 to 255), slowing for a sharp turn unless keep is set. Kept
// for the ticks to the next full look; deep sea ahead stops it till then.
static void SEC_BOTS steer(const Tank *k, Bot *b, uint16_t want, int16_t tsp, int16_t dead, uint8_t keep,
	GameInput *in)
{
	b->wantHi = (uint8_t)(want >> 8);
	b->wantLo = (uint8_t)want;
	b->tsp = (uint8_t)tsp;
	b->dead = (uint8_t)dead;
	b->steerBits = (uint8_t)((b->steerBits & (SB_FIRE | SB_TIME)) | SB_ON | (keep ? SB_KEEP : 0) |
		(tsp && sea_ahead(k) ? SB_SEA : 0));
	steer_keys(k, b, in);
}

// Whether the point px, py (world units, from the tank) lies between the
// tank and dx, dy and within w units of the line.
static uint8_t SEC_BOTS on_line(int16_t dx, int16_t dy, uint16_t len, int32_t px, int32_t py, int16_t w)
{
	int32_t dot, cross;

	// A point within 16 bits takes 16-bit multiplies, muls on a 68k.
	if (px >= -32768L && px <= 32767 && py >= -32768L && py <= 32767) {
		int16_t x = (int16_t)px, y = (int16_t)py;

		dot = (int32_t)dx * x + (int32_t)dy * y;
		if (dot <= 0 || dot >= (int32_t)dx * dx + (int32_t)dy * dy)
			return 0;
		cross = (int32_t)dx * y - (int32_t)dy * x;
	} else {
		dot = (int32_t)dx * px + (int32_t)dy * py;
		if (dot <= 0 || dot >= (int32_t)dx * dx + (int32_t)dy * dy)
			return 0;
		cross = (int32_t)dx * py - (int32_t)dy * px;
	}
	if (cross < 0)
		cross = -cross;
	// w is above 0.
	return cross < (int32_t)((uint32_t)(uint16_t)w * len);
}

// The line from the tank to x, y has no wall, forest or other live pill
// across it for a shell.
static uint8_t SEC_BOTS line_clear(const GameState *s, const Tank *k, uint16_t x, uint16_t y)
{
	int16_t dx = (int16_t)(x - k->x), dy = (int16_t)(y - k->y);
	uint16_t len = wdist(k->x, k->y, x, y), i;
	uint8_t ex = (uint8_t)(x >> 8), ey = (uint8_t)(y >> 8);

	if (!line_open(k->x, k->y, x, y))
		return 0;
	for (i = 0; i < s->map.numPills; i++) {
		const MapPill *p = &s->map.pills[i];

		if (!p->active || p->inTank || p->armour == 0 || (p->x == ex && p->y == ey))
			continue;
		if (on_line(dx, dy, len, (int32_t)mid(p->x) - k->x, (int32_t)mid(p->y) - k->y, 160))
			return 0;
	}
	return 1;
}

// Steps the gunsight towards range units, from now to the next full look.
static void SEC_BOTS sight(Bot *b, uint16_t range)
{
	uint16_t want = (uint16_t)(range / 128 + 2);

	if (want > R_GUNSIGHT_MAX)
		want = R_GUNSIGHT_MAX;
	b->sightWant = (uint8_t)want;
}

// Fires when facing aim closely enough for something at range.
static void SEC_BOTS fire(GameState *s, uint8_t t, uint16_t aim, uint16_t range, GameInput *in)
{
	const Tank *k = &s->tanks[t];
	Bot *b = &s->bots[t];
	int16_t d = turn_to(aim, k->angle);
	uint16_t tol = div32_16(100UL * 1304UL, range < 256 ? 256 : range);
	int16_t dx, dy;
	uint8_t i;

	if (d < 0)
		d = (int16_t)-d;
	if ((uint16_t)d > tol || k->reload || !k->shells || b->react)
		return;
	// Not through an allied tank. Shells hit any tank but the owner's.
	util_distance(&dx, &dy, aim, range);
	for (i = 0; i < s->numTanks; i++) {
		const Tank *o = &s->tanks[i];

		if (i != t && !o->destroyed && !o->deathWait && allied(s, o->player, k->player) &&
		    on_line(dx, dy, range, (int32_t)o->x - k->x, (int32_t)o->y - k->y, 224))
			return;
	}
	in->keys |= IN_FIRE;
	roll_aim(s, b);
}

// fire, and fires the same way when it has reloaded before the next full
// look.
static void SEC_BOTS shoot(GameState *s, uint8_t t, uint16_t aim, uint16_t range, GameInput *in)
{
	Bot *b = &s->bots[t];

	b->fireHi = (uint8_t)(aim >> 8);
	b->fireLo = (uint8_t)aim;
	b->fireRange = (uint8_t)(range > 4095 ? 255 : range >> 4);
	b->steerBits |= SB_FIRE;
	fire(s, t, aim, range, in);
}

// Where it aims: a foe in range, the pill or base of its goal in range.
// 0 if nothing.
enum { AIM_NONE, AIM_TANK, AIM_PILL, AIM_BASE };
static uint8_t SEC_BOTS aim_at(GameState *s, uint8_t t, uint16_t *ax, uint16_t *ay)
{
	const Tank *k = &s->tanks[t];
	const Bot *b = &s->bots[t];

	if (!k->shells)
		return AIM_NONE;
	if (b->foe != NO_TANK) {
		const Tank *o = &s->tanks[b->foe];
		uint16_t d = wdist(k->x, k->y, o->x, o->y);

		if (!o->destroyed && !o->deathWait && d <= GUN_RANGE) {
			uint16_t lead = (uint16_t)(((uint32_t)(o->speed >> 2) * (d >> 5) * kLevel[b->level].lead) >> 1);
			int16_t lx, ly;

			if (lead > 1023)
				lead = 1023;
			util_distance(&lx, &ly, (uint16_t)(util_dir16(o->angle) * 16 * ANGLE_ONE), lead);
			*ax = (uint16_t)(o->x + lx);
			*ay = (uint16_t)(o->y + ly);
			return AIM_TANK;
		}
	}
	if (b->goal == BG_PILL) {
		const MapPill *p = &s->map.pills[b->target];

		*ax = mid(p->x);
		*ay = mid(p->y);
		if (p->armour > 0 && wdist(k->x, k->y, *ax, *ay) <= PILL_RUN_RANGE)
			return AIM_PILL;
	}
	if (b->goal == BG_BASE) {
		const MapBase *mb = &s->map.bases[b->target];

		*ax = mid(mb->x);
		*ay = mid(mb->y);
		if (mb->owner != MAP_NEUTRAL && mb->armour > R_BASE_CAPTURE_ARMOUR &&
		    wdist(k->x, k->y, *ax, *ay) <= BASE_SHOOT_RANGE)
			return AIM_BASE;
	}
	return AIM_NONE;
}

// Asks the builder for a road on a river square just ahead, every 8 ticks.
static void SEC_BOTS bridge(GameState *s, uint8_t t, GameInput *in)
{
	const Tank *k = &s->tanks[t];
	Bot *b = &s->bots[t];
	int16_t dx, dy;
	uint8_t x, y;

	if (((s->tick + t) & 7) || k->boat || k->trees < R_LGM_COST_ROAD || b->buildWait || !builder_free(s, t) ||
	    in->build)
		return;
	// Not from in the river: he couldn't walk back to the tank.
	if (terrain((uint8_t)(k->x >> 8), (uint8_t)(k->y >> 8)) == TERRAIN_RIVER)
		return;
	util_distance(&dx, &dy, (uint16_t)(util_dir16(k->angle) * 16 * ANGLE_ONE), 256);
	x = (uint8_t)((uint16_t)(k->x + dx) >> 8);
	y = (uint8_t)((uint16_t)(k->y + dy) >> 8);
	if (terrain(x, y) != TERRAIN_RIVER || g_base_at(s, x, y) >= 0 || !cost(s, x, y, 0))
		return;
	in->build = BUILD_ROAD;
	in->buildX = x;
	in->buildY = y;
	b->buildWait = 60;
}

// Steers along the grid: from the tank's square to the middle of the next
// one towards x, y, going straight where a diagonal would clip a corner.
static uint8_t SEC_BOTS next_square(const GameState *s, const Tank *k, uint8_t *x, uint8_t *y)
{
	uint8_t tx = (uint8_t)(k->x >> 8), ty = (uint8_t)(k->y >> 8);
	int8_t sx = *x > tx ? 1 : *x < tx ? -1 : 0, sy = *y > ty ? 1 : *y < ty ? -1 : 0;

	if (sx && sy) {
		uint8_t escape = !k->boat && !cost(s, tx, ty, 0);
		uint8_t a = step_cost(s, (uint8_t)(tx + sx), ty, k->boat, escape);
		uint8_t c = step_cost(s, tx, (uint8_t)(ty + sy), k->boat, escape);

		if (!a && !c)
			return 0;
		if (!a)
			sx = 0;
		else if (!c)
			sy = 0;
	}
	*x = (uint8_t)(tx + sx);
	*y = (uint8_t)(ty + sy);
	return 1;
}

// A tick off the mode's time, as the modes count it: escaping while it
// navigates, a run and a veer while they go on. Returns the ticks left.
static inline uint8_t SEC_BOTS count_down(Bot *b)
{
	if (b->modeTime)
		b->modeTime--;
	return b->modeTime;
}

static void SEC_BOTS navigate(GameState *s, uint8_t t, GameInput *in)
{
	const Tank *k = &s->tanks[t];
	Bot *b = &s->bots[t];
	uint8_t tx = (uint8_t)(k->x >> 8), ty = (uint8_t)(k->y >> 8), px, py, gd, stopAt;
	int16_t tsp, turn;
	uint16_t want;

	if (b->goal == BG_NONE) {
		steer(k, b, k->angle, 0, 128, 0, in);
		return;
	}
	if (b->mode == BM_ESCAPE) {
		if (count_down(b) == 0) {
			b->mode = BM_NAV;
			b->want = 1;
		} else {
			b->steerBits |= SB_TIME;
		}
		steer(k, b, (uint16_t)((uint16_t)(b->veer & 31) << 8), 24, 128, 0, in);
		return;
	}
	gd = cheb(tx, ty, b->goalX, b->goalY);
	stopAt = b->goal == BG_REFUEL || b->goal == BG_PATROL || b->goal == BG_MAN;
	if (gd == 0 && (stopAt || b->goal == BG_BASE || b->goal == BG_PILL)) {
		if (b->goal == BG_REFUEL && wdist(k->x, k->y, mid(tx), mid(ty)) > 32) {
			steer(k, b, util_angle(k->x, k->y, mid(tx), mid(ty)), 8, 128, 0, in);
			return;
		}
		// Waiting at a pill's stage with no clear shot counts as failing.
		if (b->goal == BG_PILL && mod32_16(s->tick + t, 24) == 0)
			b->fails++;
		steer(k, b, k->angle, 0, 128, 0, in);
		return;
	}
	if (b->pathX != b->goalX || b->pathY != b->goalY)
		b->wayN = 0;
	// Drop waypoints reached, or passed close by when the one after is
	// nearer.
	while (b->wayN > 0) {
		uint16_t wx = mid(b->way[0][0]), wy = mid(b->way[0][1]);
		uint16_t dx = k->x > wx ? k->x - wx : wx - k->x, dy = k->y > wy ? k->y - wy : wy - k->y;
		uint8_t i, pop = dx < 160 && dy < 160 && cheb(tx, ty, b->way[0][0], b->way[0][1]) == 0;

		if (!pop && b->wayN > 1 && cheb(tx, ty, b->way[0][0], b->way[0][1]) <= 1)
			pop = cheb(tx, ty, b->way[1][0], b->way[1][1]) <
				cheb(b->way[0][0], b->way[0][1], b->way[1][0], b->way[1][1]);
		if (!pop)
			break;
		for (i = 0; i + 1 < b->wayN; i++) {
			b->way[i][0] = b->way[i + 1][0];
			b->way[i][1] = b->way[i + 1][1];
		}
		b->wayN--;
	}
	// The next part of the path is asked for a waypoint early, so the tank
	// needn't stop for it. A boat order keeps its last waypoint, the bank.
	if (s->search.who != t + 1 && !b->boatTask &&
	    (b->wayN == 0 || (b->wayN == 1 && (b->way[0][0] != b->goalX || b->way[0][1] != b->goalY))))
		b->want = 1;
	if (b->wayN) {
		px = b->way[0][0];
		py = b->way[0][1];
		tsp = FULL_SPEED;
		if (b->wayN >= 2 && cheb(tx, ty, px, py) <= 2) {
			int16_t turn = turn_to(util_angle(mid(px), mid(py), mid(b->way[1][0]), mid(b->way[1][1])),
				k->angle);

			if (turn > 1024 || turn < -1024)
				tsp = 20;
		}
	} else {
		// No path yet: slowly towards it while the way is open. With a boat
		// on order it waits at the bank.
		px = b->goalX;
		py = b->goalY;
		tsp = b->boatTask ? 0 : 12;
	}
	// Slow for the last squares, less so onto a base pills fire on, where
	// each hit would knock a crawling tank back.
	if (tsp && gd <= 2 && px == b->goalX && py == b->goalY)
		tsp = !stopAt ? 20 : b->goal == BG_REFUEL && pill_heat(s, k->player, b->goalX, b->goalY) ? 32 : 12;
	if (b->boatTask && b->wayN == 1 && cheb(tx, ty, px, py) <= 2)
		tsp = 12;
	if (b->boatTask && b->wayN == 0) {
		uint8_t bt = terrain(b->boatX, b->boatY);

		if (bt == TERRAIN_BOAT) {
			b->boatTask = 1;
			if (s->men[t].inTank || s->men[t].isDead) {
				px = b->boatX;
				py = b->boatY;
				tsp = 12;
			}
		} else if (b->boatTask == 2 && bt == TERRAIN_RIVER) {
			if (builder_free(s, t) && k->trees < R_LGM_COST_BOAT && !b->buildWait &&
			    !in->build && mod32_16(s->tick + t, 24) == 0)
				b->fails++;

			if (builder_free(s, t) && k->trees >= R_LGM_COST_BOAT && !b->buildWait && !in->build) {
				in->build = BUILD_WALL; // A wall order on a river builds a boat.
				in->buildX = b->boatX;
				in->buildY = b->boatY;
				b->buildWait = 60;
			}
		} else {
			b->boatTask = 0;
			b->want = 1;
		}
	}
	if (!next_square(s, k, &px, &py)) {
		b->wayN = 0;
		b->boatTask = 0;
		b->want = 1;
		tsp = 0;
	}
	// A boat must be fast to run aground, and slow keeps it off the bank
	// while there is no path.
	if (k->boat)
		tsp = b->wayN ? FULL_SPEED : 0;
	else if (!s->men[t].inTank && !s->men[t].isDead && b->goal != BG_MAN &&
	    terrain(px, py) == TERRAIN_RIVER && terrain(tx, ty) != TERRAIN_RIVER)
		tsp = 0;    // waits on the bank for the builder, who may be bridging it
	// With no path it keeps off water, boats and known mines.
	if (tsp && !b->wayN && !b->boatTask &&
	    (water(terrain(px, py)) || cost(s, px, py, 0) == MINE_COST))
		tsp = 0;
	if (tsp) {
		int8_t pill = g_pill_at(s, px, py), base = g_base_at(s, px, py);

		if (!step_cost(s, px, py, k->boat, !k->boat && !cost(s, tx, ty, 0)) ||
		    (pill >= 0 && s->map.pills[pill].armour > 0) ||
		    (base >= 0 && bases_cant_drive(s, (uint8_t)base, k->player))) {
			b->wayN = 0;
			b->boatTask = 0;
			b->want = 1;
			tsp = 0;
		}
	}
	want = util_angle(k->x, k->y, mid(px), mid(py));
	turn = turn_to(want, k->angle);
	// Stuck: no new square in 72 ticks while facing where it means to go,
	// or 120 at a crawl, which takes 86 ticks to cross a square. Creeping
	// on with no path doesn't count.
	if (tsp > 0 && mod32_16(s->tick + t, 24) == 0) {
		if (tx != b->lastX || ty != b->lastY || (!b->wayN && !b->boatTask)) {
			b->stuck = 0;
		} else if (turn < 1024 && turn > -1024 && ++b->stuck >= (tsp <= 12 ? 5 : 3)) {
			b->stuck = 0;
			b->mode = BM_ESCAPE;
			b->modeTime = 40;
			b->veer = (uint8_t)((k->angle >> 8) + 12 + rnd(s, 9));
			b->wayN = 0;
			b->boatTask = 0;
			// Away at once, counting from the next tick.
			b->steerBits |= SB_TIME;
			steer(k, b, (uint16_t)((uint16_t)(b->veer & 31) << 8), 24, 128, 0, in);
			return;
		}
		b->lastX = tx;
		b->lastY = ty;
	}
	steer(k, b, want, tsp, 128, 0, in);
	if (!b->boatTask)
		bridge(s, t, in);
}

// Turns back from a pill, round by the side further from north, as the
// pill aims as if the tank were going north.
static void SEC_BOTS start_veer(GameState *s, Bot *b, uint16_t aim)
{
	uint16_t a1 = (uint16_t)((aim + ANGLE_MAX * 3 / 8) & (ANGLE_MAX - 1));
	uint16_t a2 = (uint16_t)((aim - ANGLE_MAX * 3 / 8) & (ANGLE_MAX - 1));
	uint16_t n1 = a1 > ANGLE_MAX / 2 ? (uint16_t)(ANGLE_MAX - a1) : a1;
	uint16_t n2 = a2 > ANGLE_MAX / 2 ? (uint16_t)(ANGLE_MAX - a2) : a2;
	uint16_t pick = n1 > n2 + 512 ? a1 : n2 > n1 + 512 ? a2 : rnd(s, 2) ? a1 : a2;

	b->veer = (uint8_t)(pick >> 8);
	b->mode = BM_VEER;
	b->modeTime = 250;
}

// A run at the live pill of its goal: in at full speed firing, then away
// across the run until out of its reach, then back to a stage.
static void SEC_BOTS pill_attack(GameState *s, uint8_t t, GameInput *in)
{
	const Tank *k = &s->tanks[t];
	Bot *b = &s->bots[t];
	const MapPill *p = &s->map.pills[b->target];
	uint16_t px = mid(p->x), py = mid(p->y), range = wdist(k->x, k->y, px, py);
	uint16_t aim = util_angle(k->x, k->y, px, py);

	if (b->mode == BM_NAV) {
		b->mode = BM_RUN;
		b->modeTime = 200;
	}
	if (b->mode == BM_RUN) {
		// A cornered tank runs on however low its armour.
		if (range >= PILL_VEER_RANGE && count_down(b) &&
		    (k->armour >= kLevel[b->level].retreat || run_ok(s, t))) {
			uint16_t a = (uint16_t)((aim + (int8_t)b->aimErr * 4) & (ANGLE_MAX - 1));

			b->steerBits |= SB_TIME;
			sight(b, range);
			steer(k, b, a, FULL_SPEED, 8, 1, in);
			if (range <= GUN_RANGE + 160)
				shoot(s, t, a, range, in);
			return;
		}
		start_veer(s, b, aim);
	}
	if (range > R_PILL_RANGE + 256 || count_down(b) == 0) {
		uint8_t gx, gy;

		b->mode = BM_NAV;
		b->think = 0;
		if (pill_stage(s, t, b->target, &gx, &gy))
			set_goal(b, BG_PILL, b->target, gx, gy);
		else
			b->goal = BG_NONE;
		navigate(s, t, in);
		return;
	}
	b->steerBits |= SB_TIME;
	steer(k, b, (uint16_t)((uint16_t)b->veer << 8), FULL_SPEED, 64, 1, in);
}

static void SEC_BOTS drive(GameState *s, uint8_t t, GameInput *in)
{
	const Tank *k = &s->tanks[t];
	Bot *b = &s->bots[t];
	uint16_t ax = 0, ay = 0, range = R_GUNSIGHT_MAX * 128, aim;
	uint8_t what, tx, ty, hit;

	in->keys = 0;
	in->opts = 0;
	in->build = BUILD_NONE;
	if (k->destroyed || k->deathWait) {
		if (b->goal != BG_NONE || b->wayN) {
			b->goal = BG_NONE;
			b->boatTask = 0;
			b->wayN = 0;
			b->want = 0;
			b->mode = BM_NAV;
			b->foe = NO_TANK;
		}
		// Nothing kept from before it died, so it doesn't fire along its
		// old aim when it comes back.
		b->steerBits = 0;
		b->sightWant = 0;
		if (s->search.who == t + 1)
			s->search.who = 0;
		return;
	}
	if (b->react)
		b->react--;
	if (b->buildWait)
		b->buildWait--;
	if (b->skipTime && --b->skipTime == 0)
		b->skip = 0;
	if (b->think)
		b->think--;
	tx = (uint8_t)(k->x >> 8);
	ty = (uint8_t)(k->y >> 8);
	hit = k->armour < b->lastArmour;
	b->lastArmour = k->armour;
	// Between full looks it keeps steering by the last one, unless it was
	// hit, reached a new square, got on or off a boat or its foe died.
	if (((s->tick + t) & (BOT_EVERY - 1)) && !hit && tx == b->lookX && ty == b->lookY &&
	    k->boat == b->boat &&
	    (b->foe == NO_TANK || (!s->tanks[b->foe].destroyed && !s->tanks[b->foe].deathWait))) {
		if (b->steerBits & SB_TIME)
			count_down(b);
		steer_keys(k, b, in);
		if ((b->steerBits & SB_FIRE) && !k->reload)
			fire(s, t, (uint16_t)(((uint16_t)b->fireHi << 8) | b->fireLo), (uint16_t)(b->fireRange << 4), in);
		return;
	}
	b->lookX = tx;
	b->lookY = ty;
	b->steerBits = 0;
	b->sightWant = 0;
	if (k->boat != b->boat) {
		b->boat = k->boat;
		b->boatTask = 0;
		b->wayN = 0;
		if (s->search.who == t + 1)
			s->search.who = 0;
		b->want = 1;
	}
	if (b->goal == BG_MAN && s->men[t].inTank)
		b->think = 0;
	if (!b->think) {
		think(s, t, in);
		b->think = (uint8_t)(level_think(b->level) + 1);
	}
	if (b->goal == BG_PILL && s->map.pills[b->target].armour == 0 &&
	    (b->goalX != s->map.pills[b->target].x || b->goalY != s->map.pills[b->target].y))
		set_goal(b, BG_PILL, b->target, s->map.pills[b->target].x, s->map.pills[b->target].y);
	// Breaks off a fight to refuel at once; a run at a pill turns away
	// first.
	if (k->armour < kLevel[b->level].retreat && b->goal != BG_REFUEL && b->mode != BM_NAV &&
	    b->mode != BM_RUN && b->mode != BM_VEER) {
		b->mode = BM_NAV;
		b->think = 0;
	}
	if (b->goal == BG_PILL && s->map.pills[b->target].armour > 0 && b->mode != BM_ESCAPE) {
		const MapPill *p = &s->map.pills[b->target];

		if (b->mode == BM_RUN || b->mode == BM_VEER ||
		    (cheb((uint8_t)(k->x >> 8), (uint8_t)(k->y >> 8), b->goalX, b->goalY) <= 1 &&
		     (b->goalX != p->x || b->goalY != p->y) && run_ok(s, t))) {
			pill_attack(s, t, in);
			return;
		}
	} else if (b->mode == BM_RUN || b->mode == BM_VEER) {
		b->mode = BM_NAV;
	}
	what = aim_at(s, t, &ax, &ay);
	if (what != AIM_NONE) {
		range = wdist(k->x, k->y, ax, ay);
		if (((s->tick + t) & (BOT_EVERY - 1)) == 0)
			b->clear = line_clear(s, k, ax, ay);
	}
	sight(b, range);
	if (what == AIM_NONE || !b->clear) {
		navigate(s, t, in);
		return;
	}
	aim = (uint16_t)((util_angle(k->x, k->y, ax, ay) + (int8_t)b->aimErr * 4) & (ANGLE_MAX - 1));
	// Not stood still by the sea, where a shell's knock could push it in:
	// it goes on its way, shooting when it faces the target.
	if (!k->boat && !cost(s, (uint8_t)(k->x >> 8), (uint8_t)(k->y >> 8), 0)) {
		navigate(s, t, in);
		shoot(s, t, aim, range, in);
		return;
	}
	switch (what) {
	case AIM_TANK:
		// On a boat one hit sinks it: it goes on its way, shooting when
		// it faces the foe.
		if (k->boat) {
			navigate(s, t, in);
			shoot(s, t, aim, range, in);
			break;
		}
		// Closes in when far, else holds and shoots; stays put on a base
		// it is refuelling at.
		steer(k, b, aim, range > 1024 && b->goal != BG_REFUEL ? 24 : 0, 8, 0, in);
		shoot(s, t, aim, range, in);
		break;
	case AIM_PILL:
		// In reach of the pill on the way to a stage: runs in from here if
		// that isn't along the north-south line and there is room.
		if (!run_north(aim) && run_ok(s, t) && range > PILL_VEER_RANGE + 512)
			pill_attack(s, t, in);
		else
			navigate(s, t, in);
		break;
	default:
		steer(k, b, aim, 0, 8, 0, in);
		shoot(s, t, aim, range, in);
		break;
	}
}

void SEC_BOTS bot_reset(GameState *s, uint8_t t, uint8_t level)
{
	Bot *b = &s->bots[t];
	uint8_t *p = &b->level;
	uint8_t i;

	for (i = 0; i < sizeof(Bot); i++)
		p[i] = 0;
	b->level = level;
	b->foe = NO_TANK;
	b->boat = s->tanks[t].boat;
	b->lastX = (uint8_t)(s->tanks[t].x >> 8);
	b->lastY = (uint8_t)(s->tanks[t].y >> 8);
	// Bots look round on different ticks.
	b->think = (uint8_t)((t * 5) % level_think(level));
	if (s->search.who == t + 1)
		s->search.who = 0;
}

void SEC_BOTS bots_tick(GameState *s, const GameInput *in, GameInput *out)
{
	BotSearch *q = &s->search;
	uint8_t i;

	for (i = 0; i < s->numTanks; i++) {
		out[i] = in[i];
		if (s->botMask & (1 << i)) {
			drive(s, i, &out[i]);
		}
	}
	// The search serves the bots that want a path in turn.
	if (!q->who) {
		for (i = 0; i < s->numTanks; i++) {
			uint8_t t = (uint8_t)((q->next + i) % s->numTanks);

			if ((s->botMask & (1 << t)) && s->bots[t].want && !s->tanks[t].destroyed &&
			    !s->tanks[t].deathWait && s->bots[t].goal != BG_NONE) {
				search_begin(s, t);
				q->next = (uint8_t)(t + 1);
				break;
			}
		}
	}
	if (q->who) {
		uint8_t n = 0;

		for (i = 0; i < s->numTanks; i++)
			n = (uint8_t)(n + ((s->botMask >> i) & 1));
		SEARCH_MARK(0);
		search_step(s, n * BOT_BUDGET_EACH > BOT_BUDGET ? (uint8_t)(n * BOT_BUDGET_EACH) : BOT_BUDGET);
		SEARCH_MARK(1);
	}
}
