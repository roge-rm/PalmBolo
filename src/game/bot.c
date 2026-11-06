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
#define BOT_BUDGET      12      // search cells expanded a tick, all bots together, at least
#define BOT_BUDGET_EACH 3       // or this many for each bot, if more
#define BOT_MAX_CELLS   600     // a search gives up and takes the best so far after this many
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
static const int8_t kDX[8] = { 0, 1, 1, 1, 0, -1, -1, -1 };
static const int8_t kDY[8] = { -1, -1, 0, 1, 1, 1, 0, -1 };

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

// Map rows fetched as needed, five from y0 on, so a cell's neighbours cost
// few calls into code segment 1.
typedef struct Rows {
	uint8_t y0;
	const uint8_t *r[5];
} Rows;

static inline void SEC_BOTS rows_init(Rows *c, uint8_t y0)
{
	c->y0 = y0;
	c->r[0] = c->r[1] = c->r[2] = c->r[3] = c->r[4] = 0;
}

static inline const uint8_t *SEC_BOTS row_of(Rows *c, uint8_t y)
{
	uint8_t i = (uint8_t)(y - c->y0);

	if (!c->r[i])
		c->r[i] = mapstore_row(y);
	return c->r[i];
}

// The same for 16 rows round a pill, terrain as game_terrain gives it.
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

// What a square costs to cross: 0 it can't be, else 1 plus the ticks it
// waits in the queue. Walls are out, and off a boat so is deep sea and
// anything beside it, as a fast tank can't stop short. A known mine costs
// a lot, so a tank shut in by mines still gets out. c holds rows y - 1 to
// y + 1 at least.
static inline uint8_t SEC_BOTS cost_rows(const GameState *s, Rows *c, uint8_t x, uint8_t y, uint8_t boat)
{
	const uint8_t *row;
	uint8_t t;

	if (x <= MAP_EDGE_LOW + 1 || x >= MAP_EDGE_HIGH - 1 || y <= MAP_EDGE_LOW + 1 ||
	    y >= MAP_EDGE_HIGH - 1)
		return 0;
	row = row_of(c, y);
	t = row[x];
	if (t >= TERRAIN_MINE_FIRST && t <= TERRAIN_MINE_LAST) {
		if (!s->hiddenMines || mapstore_flag(MS_KNOWN, x, y))
			return MINE_COST;
		t = (uint8_t)(t - TERRAIN_MINE_ADD);
	}
	if (is_wall(t))
		return 0;
	// A boat is fast on water and leaves the boat behind on land.
	if (boat)
		return t == TERRAIN_RIVER || t == TERRAIN_DEEP_SEA || t == TERRAIN_BOAT ? 1 : 2;
	if (t == TERRAIN_DEEP_SEA || row[x - 1] == TERRAIN_DEEP_SEA || row[x + 1] == TERRAIN_DEEP_SEA ||
	    row_of(c, (uint8_t)(y - 1))[x] == TERRAIN_DEEP_SEA ||
	    row_of(c, (uint8_t)(y + 1))[x] == TERRAIN_DEEP_SEA)
		return 0;
	switch (t) {
	case TERRAIN_RIVER:
		return 6;
	case TERRAIN_SWAMP:
	case TERRAIN_CRATER:
	case TERRAIN_RUBBLE:
		return 5;
	case TERRAIN_FOREST:
		return 3;
	default:
		return 1;
	}
}

static uint8_t SEC_BOTS cost(const GameState *s, uint8_t x, uint8_t y, uint8_t boat)
{
	Rows c;

	rows_init(&c, (uint8_t)(y - 1));
	return cost_rows(s, &c, x, y, boat);
}

// The window's first square on one axis: both ends in it when they are
// within 24 squares, else the tank 4 squares in from the side away from
// the goal.
static uint8_t SEC_BOTS window_origin(uint8_t from, uint8_t to)
{
	int16_t off = (int16_t)(((int16_t)to - from) / 2), o;

	if (off > 12)
		off = 12;
	if (off < -12)
		off = -12;
	o = (int16_t)(from - BOT_WIN / 2 + off);
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
		uint8_t r = kReach[y > py ? y - py : py - y], x, *row = &q->cell[y * (BOT_WIN / 2)];

		x0 = (int16_t)(px - r);
		x1 = (int16_t)(px + r);
		if (x0 < 0)
			x0 = 0;
		if (x1 >= BOT_WIN)
			x1 = BOT_WIN - 1;
		if (x0 > x1)
			continue;
		// Nibbles of new cells set to 10, odd cells in the high half, a
		// byte at a time between the ends.
		x = (uint8_t)x0;
		if (x & 1) {
			if (!(row[x >> 1] & 0xF0))
				row[x >> 1] |= 0xA0;
			x++;
		}
		for (; x < (uint8_t)x1; x += 2) {
			uint8_t *c = &row[x >> 1], v = *c;

			if (!(v & 0x0F))
				v |= 0x0A;
			if (!(v & 0xF0))
				v |= 0xA0;
			*c = v;
		}
		if (x == (uint8_t)x1 && !(row[x >> 1] & 0x0F))
			row[x >> 1] |= 0x0A;
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

// Queues window cell v to spread c rounds from now (0 to BOT_BUCKETS - 1).
// 0 if the queue is full.
static inline uint8_t SEC_BOTS q_push(BotSearch *q, uint16_t v, uint8_t c)
{
	uint8_t sl = q->free, k = (uint8_t)((q->round + c) & (BOT_BUCKETS - 1));

	if (sl == BOT_NO_SLOT)
		return 0;
	q->free = q->link[sl];
	q->queue[sl] = v;
	q->link[sl] = BOT_NO_SLOT;
	if (q->bhead[k] == BOT_NO_SLOT)
		q->bhead[k] = sl;
	else
		q->link[q->btail[k]] = sl;
	q->btail[k] = sl;
	q->count++;
	return 1;
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
	// Four bytes a pass, a quarter of the loop overhead on a 68k.
	for (j = 0, c = q->cell; j < sizeof(q->cell) / 4; j++, c += 4) {
		c[0] = 0;
		c[1] = 0;
		c[2] = 0;
		c[3] = 0;
	}
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
	for (j = 0; j < BOT_QUEUE; j++)
		q->link[j] = (uint8_t)(j + 1);
	q->link[BOT_QUEUE - 1] = BOT_NO_SLOT;
	for (i = 0; i < BOT_BUCKETS; i++)
		q->bhead[i] = BOT_NO_SLOT;
	q->free = 0;
	q->round = 0;
	q->count = 0;
	q_push(q, start, 0);
	q->best = start;
	q->bestH = cell_h(q, start);
	q->cur = 0;
	// Something to shoot needs only to be got near.
	q->near = 0;
	if ((b->goal == BG_PILL && s->map.pills[b->target].armour > 0 &&
	     b->goalX == s->map.pills[b->target].x && b->goalY == s->map.pills[b->target].y) ||
	    (b->goal == BG_BASE && bases_cant_drive(s, b->target, k->player)))
		q->near = 3;
	else if (b->goal == BG_TANK)
		q->near = 2;
	b->want = 0;
}

// The search's fields for expanding cells, copied out once a step, as gcc
// reloads them after every byte store otherwise.
typedef struct Expand {
	GameState *s;
	BotSearch *q;
	uint8_t *cell;
	uint8_t ox, oy, goalX, goalY, boat, hidden, far, ex, ey, bestH, near;
	uint8_t inner;      // the window is clear of the map's edge band
} Expand;

static void SEC_BOTS expand_init(Expand *e, GameState *s)
{
	BotSearch *q = &s->search;
	uint8_t gx, gy;

	e->s = s;
	e->q = q;
	e->cell = q->cell;
	e->ox = q->ox;
	e->oy = q->oy;
	e->goalX = q->goalX;
	e->goalY = q->goalY;
	e->boat = q->boat;
	e->hidden = s->hiddenMines;
	e->bestH = q->bestH;
	e->near = q->near;
	gx = (uint8_t)(e->goalX - e->ox);
	gy = (uint8_t)(e->goalY - e->oy);
	e->far = gx >= BOT_WIN || gy >= BOT_WIN;
	e->ex = gx >= BOT_WIN ? (gx < 144 ? BOT_WIN - 1 : 0) : 255;
	e->ey = gy >= BOT_WIN ? (gy < 144 ? BOT_WIN - 1 : 0) : 255;
	e->inner = e->ox > MAP_EDGE_LOW + 1 && e->ox + BOT_WIN - 1 < MAP_EDGE_HIGH - 1 &&
		e->oy > MAP_EDGE_LOW + 1 && e->oy + BOT_WIN - 1 < MAP_EDGE_HIGH - 1;
}

// Land a tank crosses, by terrain 0 to 9 (no mines): 0 for walls, else
// 1 plus the ticks it waits in the queue.
static const uint8_t kLandCost[10] = { 0, 6, 5, 5, 1, 3, 5, 1, 0, 1 };

// Window cell n, new to the search or new in a pill's reach (v, its nibble
// at p), seen in direction d from the cell being expanded: queued if it can
// be crossed, at the cost cost_rows gives. Returns as expand_dir.
static uint8_t SEC_BOTS expand_new(Expand *e, uint16_t n, uint8_t d, uint8_t v, uint8_t *p)
{
	uint8_t nx = (uint8_t)(n & (BOT_WIN - 1)), ny = (uint8_t)(n >> 5);
	uint8_t x = (uint8_t)(e->ox + nx), y = (uint8_t)(e->oy + ny), c, t, h;
	const uint8_t *row;

	if (!e->inner && (x <= MAP_EDGE_LOW + 1 || x >= MAP_EDGE_HIGH - 1 ||
	    y <= MAP_EDGE_LOW + 1 || y >= MAP_EDGE_HIGH - 1))
		goto solid;
	row = mapstore_row(y);
	t = row[x];
	if (t >= TERRAIN_MINE_FIRST && t <= TERRAIN_MINE_LAST) {
		if (!e->hidden || mapstore_flag(MS_KNOWN, x, y)) {
			c = MINE_COST;
			goto queue;
		}
		t = (uint8_t)(t - TERRAIN_MINE_ADD);
	}
	if (is_wall(t))
		goto solid;
	if (e->boat) {
		c = t == TERRAIN_RIVER || t == TERRAIN_DEEP_SEA || t == TERRAIN_BOAT ? 1 : 2;
		goto queue;
	}
	if (t == TERRAIN_DEEP_SEA || row[x - 1] == TERRAIN_DEEP_SEA || row[x + 1] == TERRAIN_DEEP_SEA ||
	    mapstore_row((uint8_t)(y - 1))[x] == TERRAIN_DEEP_SEA ||
	    mapstore_row((uint8_t)(y + 1))[x] == TERRAIN_DEEP_SEA)
		goto solid;
	c = t < 10 ? kLandCost[t] : 1;
queue:
	if (v == 10)
		c = (uint8_t)(c + ZONE_WAIT);
	if (c > BOT_BUCKETS - 1)
		c = BOT_BUCKETS - 1;
	if (!q_push(e->q, n, c))
		return 1;
	*p = (uint8_t)((n & 1) ? (*p & 0x0F) | ((d + 1) << 4) : (*p & 0xF0) | (d + 1));
	// As cell_h: with the goal out of the window, only cells on the edge
	// facing it (ex, ey; 255 for none) are taken at their distance.
	h = cheb(x, y, e->goalX, e->goalY);
	if (e->far && nx != e->ex && ny != e->ey)
		h = (uint8_t)(h > 255 - 32 ? 255 : h + 32);
	if (h < e->bestH) {
		e->bestH = h;
		e->q->bestH = h;
		e->q->best = n;
		if (h <= e->near)
			return 2;
	}
	return 1;
solid:
	*p |= (uint8_t)((n & 1) ? 0xF0 : 0x0F);
	return 0;
}

// Tries the neighbour step cells on from cell idx, in direction d. 0 if it
// is solid, 1 if not, 2 when it reaches the goal. The caller checks it is
// in the window.
static inline uint8_t SEC_BOTS expand_dir(Expand *e, uint16_t idx, uint8_t d, int16_t step)
{
	uint16_t n = (uint16_t)(idx + step);
	uint8_t *p = &e->cell[n >> 1], v = (uint8_t)((n & 1) ? *p >> 4 : *p & 15);

	if (v == 15)
		return 0;
	if (v != 0 && v != 10)
		return 1;
	return expand_new(e, n, d, v, p);
}

// Expands one cell, straight neighbours first, then each diagonal with
// both corners beside it open. Returns 1 when the goal is reached. The
// directions are written out, so each has its step as a constant.
static uint8_t SEC_BOTS expand(Expand *e, uint16_t idx)
{
	uint8_t lx = (uint8_t)(idx & (BOT_WIN - 1)), ly = (uint8_t)(idx >> 5);
	uint8_t w = lx > 0, east = lx < BOT_WIN - 1, north = ly > 0, south = ly < BOT_WIN - 1;
	uint8_t o0 = 0, o2 = 0, o4 = 0, o6 = 0;

	if ((north && (o0 = expand_dir(e, idx, 0, -BOT_WIN)) == 2) ||
	    (east && (o2 = expand_dir(e, idx, 2, 1)) == 2) ||
	    (south && (o4 = expand_dir(e, idx, 4, BOT_WIN)) == 2) ||
	    (w && (o6 = expand_dir(e, idx, 6, -1)) == 2))
		return 1;
	return (o0 && o2 && expand_dir(e, idx, 1, 1 - BOT_WIN) == 2) ||
		(o2 && o4 && expand_dir(e, idx, 3, 1 + BOT_WIN) == 2) ||
		(o4 && o6 && expand_dir(e, idx, 5, BOT_WIN - 1) == 2) ||
		(o6 && o0 && expand_dir(e, idx, 7, -1 - BOT_WIN) == 2);
}

static void SEC_BOTS trace_begin(BotSearch *q)
{
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
	b->boat = q->boat;
	if (q->bestH >= startH)
		b->fails++;
	else
		b->fails = 0;
}

// Runs the search on by budget, in quarters: a cell expanded costs 5, an
// empty round and a trace step 1, a pill zone 16.
static void SEC_BOTS search_step(GameState *s, uint8_t budget)
{
	BotSearch *q = &s->search;
	uint16_t units = (uint16_t)budget * 4;
	Expand e;

	if (!q->who)
		return;
	expand_init(&e, s);
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
			uint8_t sl;

			if (q->count == 0) {
				trace_begin(q);
				continue;
			}
			sl = q->bhead[q->round];
			if (sl == BOT_NO_SLOT) {
				// Nothing left this round: on to the next.
				q->round = (uint8_t)((q->round + 1) & (BOT_BUCKETS - 1));
				units--;
				continue;
			}
			q->bhead[q->round] = q->link[sl];
			q->link[sl] = q->free;
			q->free = sl;
			q->count--;
			units = (uint16_t)(units > 5 ? units - 5 : 0);
			if (expand(&e, q->queue[sl]) || ++q->cur >= BOT_MAX_CELLS)
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

// The nearest base of its side that can give something, or -1.
static int8_t SEC_BOTS fuel_base(const GameState *s, uint8_t t)
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
		if (p->armour > 0 && k->shells < 10)
			continue;
		sc = (int16_t)(cheb(tx, ty, p->x, p->y) * 4 + rnd(s, 8));
		sc = (int16_t)(p->armour == 0 ? sc - 24 : sc + 8 + p->armour * 2);
		if (sc < best) {
			best = sc;
			kind = BK_PILL;
			idx = i;
			gx = p->x;
			gy = p->y;
		}
	}
	// With no base yet it dashes for a neutral one, guarded or not.
	if (kind && !(kind == BK_PILL && s->map.pills[idx].armour > 0) && k->shells >= 10 &&
	    !(kind == BK_BASE && s->map.bases[idx].owner == MAP_NEUTRAL && fuel_base(s, t) < 0)) {
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
	// Nothing to take: drive round own bases.
	if (s->map.numBases > 0) {
		i = rnd(s, s->map.numBases);
		if (s->map.bases[i].active) {
			set_goal(b, BG_PATROL, i, (uint8_t)(s->map.bases[i].x + rnd(s, 5) - 2),
				(uint8_t)(s->map.bases[i].y + rnd(s, 5) - 2));
			return;
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
	if (k->trees < 10) {
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
	if ((b->goal == BG_BASE || b->goal == BG_PILL || b->goal == BG_REFUEL) &&
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
		b->fails = 0;
	}
	if (b->goal == BG_REFUEL) {
		const MapBase *mb = &s->map.bases[b->target];
		uint8_t on = (k->x >> 8) == mb->x && (k->y >> 8) == mb->y;

		if (mb->owner != MAP_NEUTRAL && allied(s, mb->owner, k->player) &&
		    (k->armour < R_TANK_FULL_ARMOUR || k->shells < R_TANK_FULL_SHELLS - 4) &&
		    !(on && mb->armour < R_BASE_MIN_ARMOUR + R_BASE_ARMOUR_GIVE && mb->shells == 0))
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
			if (b->goal == BG_PATROL)
				return;
		}
	}
	if (goal_ok(s, t))
		return;
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
		else if (ad > 1024 && tsp > 8)
			tsp = 8;
	} else if (ad > 2048 && tsp > 16) {
		tsp = 16;
	}
	if (b->steerBits & SB_SEA)
		tsp = 0;
	if (k->speed < tsp)
		in->keys |= IN_FASTER;
	else if (k->speed > tsp + 3)
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
static void SEC_BOTS next_square(const GameState *s, const Tank *k, uint8_t *x, uint8_t *y)
{
	uint8_t tx = (uint8_t)(k->x >> 8), ty = (uint8_t)(k->y >> 8);
	int8_t sx = *x > tx ? 1 : *x < tx ? -1 : 0, sy = *y > ty ? 1 : *y < ty ? -1 : 0;

	if (sx && sy) {
		Rows rc;
		uint8_t a, c;

		rows_init(&rc, (uint8_t)(ty - 2));
		a = cost_rows(s, &rc, (uint8_t)(tx + sx), ty, k->boat);
		c = cost_rows(s, &rc, tx, (uint8_t)(ty + sy), k->boat);

		if (!a && c)
			sx = 0;
		else if (!c && a)
			sy = 0;
	}
	*x = (uint8_t)(tx + sx);
	*y = (uint8_t)(ty + sy);
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
	if (s->search.who != t + 1 &&
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
		// No path yet: slowly towards it while the way is open.
		px = b->goalX;
		py = b->goalY;
		tsp = 12;
	}
	if (gd <= 2 && px == b->goalX && py == b->goalY)
		tsp = stopAt ? 12 : 20;
	next_square(s, k, &px, &py);
	// A boat must be fast to run aground, and slow keeps it off the bank
	// while there is no path.
	if (k->boat)
		tsp = b->wayN || cost(s, px, py, 1) == 1 ? FULL_SPEED : 12;
	else if (!s->men[t].inTank && !s->men[t].isDead && b->goal != BG_MAN &&
	    terrain(px, py) == TERRAIN_RIVER && terrain(tx, ty) != TERRAIN_RIVER)
		tsp = 0;    // waits on the bank for the builder, who may be bridging it
	if (!b->wayN && !cost(s, px, py, k->boat))
		tsp = 0;
	want = util_angle(k->x, k->y, mid(px), mid(py));
	turn = turn_to(want, k->angle);
	// Stuck: no new square in 72 ticks while facing where it means to go.
	if (tsp > 0 && mod32_16(s->tick + t, 24) == 0) {
		if (tx != b->lastX || ty != b->lastY) {
			b->stuck = 0;
		} else if (turn < 1024 && turn > -1024 && ++b->stuck >= 3) {
			b->stuck = 0;
			b->mode = BM_ESCAPE;
			b->modeTime = 40;
			b->veer = (uint8_t)((k->angle >> 8) + 12 + rnd(s, 9));
			b->wayN = 0;
			// Away at once, counting from the next tick.
			b->steerBits |= SB_TIME;
			steer(k, b, (uint16_t)((uint16_t)(b->veer & 31) << 8), 24, 128, 0, in);
			return;
		}
		b->lastX = tx;
		b->lastY = ty;
	}
	steer(k, b, want, tsp, 128, 0, in);
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
		if (range >= PILL_VEER_RANGE && count_down(b) && k->armour >= kLevel[b->level].retreat) {
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
		b->wayN = 0;
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
		     (b->goalX != p->x || b->goalY != p->y) && run_ready(k, b))) {
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
		if (!run_north(aim) && run_ready(k, b) && range > PILL_VEER_RANGE + 512)
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
		search_step(s, n * BOT_BUDGET_EACH > BOT_BUDGET ? (uint8_t)(n * BOT_BUDGET_EACH) : BOT_BUDGET);
	}
}
