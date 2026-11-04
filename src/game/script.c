// Scripted games for checking the rules: each script places the tank on a
// kind of terrain found on the map and holds keys for a number of ticks.
#include "script.h"
#include "gamepriv.h"
#include "util.h"

enum {
	OP_END,
	OP_KEYS,    // hold keys a for n ticks
	OP_FIND,    // put the tank on the first square of terrain a with terrain
	            // b c squares east (squares between are a), facing east, on
	            // a boat if d
	OP_STOCK,   // set armour a, shells b, mines c
	OP_BACK,    // move the tank a squares west
	OP_FACE,    // turn the tank to face direction a (0-15)
	OP_BASE,    // put the tank on the first base with no live pill within
	            // 9 squares, off the boat
	OP_NEARPILL, // put the tank a squares west of the first pill with
	            // grass or road there and between, facing east
	OP_PILLARM, // set that pill's armour to a
	OP_BASESTOCK, // set that base's armour a, shells b, mines c
	OP_TYPE,    // a new game of type a (GAME_OPEN etc)
	OP_AWAY,    // move the tank a squares south
	OP_TREES,   // set the tank's trees to a
	OP_BUILD,   // builder order a (BUILD_*) at the tank's square moved b, c
	            // (signed), then n ticks in all with no keys
	OP_SET,     // terrain a at the tank's square moved b, c
	OP_CARRY,   // the found pill goes into the tank, owned by it
	OP_PILLOWN, // the found pill's owner becomes a
	OP_MOVE,    // move the tank b, c squares (signed)
	OP_WRECK    // a tank wreck, big if a, at the tank's square moved b, c,
	            // heading the tank's way for n steps
};

typedef struct Op {
	uint8_t op, a, b, c, d;
	uint16_t n;
} Op;

#define F IN_FASTER
static const Op kScripts[] = {
	// 0 from the start: off the boat and round the island
	{ OP_KEYS, F, 0, 0, 0, 300 }, { OP_KEYS, F | IN_LEFT, 0, 0, 0, 60 },
	{ OP_KEYS, F, 0, 0, 0, 300 }, { OP_KEYS, F | IN_RIGHT | IN_FIRE, 0, 0, 0, 80 },
	{ OP_KEYS, F, 0, 0, 0, 300 }, { OP_END, 0, 0, 0, 0, 0 },
	// 1 speeds: road, grass, forest, swamp
	{ OP_FIND, TERRAIN_ROAD, TERRAIN_ROAD, 6, 0, 0 }, { OP_KEYS, F, 0, 0, 0, 60 },
	{ OP_FIND, TERRAIN_GRASS, TERRAIN_GRASS, 6, 0, 0 }, { OP_KEYS, F, 0, 0, 0, 60 },
	{ OP_FIND, TERRAIN_FOREST, TERRAIN_FOREST, 4, 0, 0 }, { OP_KEYS, F, 0, 0, 0, 60 },
	{ OP_FIND, TERRAIN_SWAMP, TERRAIN_SWAMP, 3, 0, 0 }, { OP_KEYS, F, 0, 0, 0, 60 },
	{ OP_END, 0, 0, 0, 0, 0 },
	// 2 boat along a river and onto land
	{ OP_FIND, TERRAIN_RIVER, TERRAIN_GRASS, 4, 1, 0 }, { OP_KEYS, F, 0, 0, 0, 120 },
	{ OP_KEYS, IN_SLOWER, 0, 0, 0, 60 }, { OP_END, 0, 0, 0, 0, 0 },
	// 3 wading through a river without a boat
	{ OP_FIND, TERRAIN_RIVER, TERRAIN_RIVER, 5, 0, 0 }, { OP_KEYS, 0, 0, 0, 0, 100 },
	{ OP_KEYS, F, 0, 0, 0, 200 }, { OP_END, 0, 0, 0, 0, 0 },
	// 4 deep sea without a boat, then the respawn
	{ OP_FIND, TERRAIN_DEEP_SEA, TERRAIN_DEEP_SEA, 2, 0, 0 }, { OP_KEYS, 0, 0, 0, 0, 10 },
	{ OP_KEYS, 0, 0, 0, 0, 260 }, { OP_END, 0, 0, 0, 0, 0 },
	// 5 shooting a building, then forest, at shortened range
	{ OP_FIND, TERRAIN_GRASS, TERRAIN_BUILDING, 3, 0, 0 },
	{ OP_KEYS, IN_SIGHT_NEAR, 0, 0, 0, 9 }, { OP_KEYS, IN_FIRE, 0, 0, 0, 200 },
	{ OP_FIND, TERRAIN_GRASS, TERRAIN_FOREST, 3, 0, 0 },
	{ OP_KEYS, IN_SIGHT_NEAR, 0, 0, 0, 9 }, { OP_KEYS, IN_FIRE, 0, 0, 0, 60 },
	{ OP_END, 0, 0, 0, 0, 0 },
	// 6 full range shot over water, and shooting grass to swamp
	{ OP_FIND, TERRAIN_DEEP_SEA, TERRAIN_DEEP_SEA, 12, 1, 0 }, { OP_KEYS, IN_FIRE, 0, 0, 0, 1 },
	{ OP_KEYS, 0, 0, 0, 0, 80 },
	{ OP_FIND, TERRAIN_GRASS, TERRAIN_GRASS, 3, 0, 0 },
	{ OP_KEYS, IN_SIGHT_NEAR, 0, 0, 0, 10 }, { OP_KEYS, IN_FIRE, 0, 0, 0, 120 },
	{ OP_END, 0, 0, 0, 0, 0 },
	// 7 lay a mine, back off, drive over it; then a chain of mines
	{ OP_FIND, TERRAIN_GRASS, TERRAIN_GRASS, 6, 0, 0 }, { OP_KEYS, IN_MINE, 0, 0, 0, 1 },
	{ OP_BACK, 1, 0, 0, 0, 0 }, { OP_KEYS, F, 0, 0, 0, 80 },
	{ OP_KEYS, IN_SLOWER, 0, 0, 0, 60 },
	{ OP_FIND, TERRAIN_ROAD, TERRAIN_ROAD, 6, 0, 0 }, { OP_KEYS, F | IN_MINE, 0, 0, 0, 90 },
	{ OP_KEYS, IN_SLOWER, 0, 0, 0, 40 }, { OP_FACE, 12, 0, 0, 0, 0 },
	{ OP_KEYS, F, 0, 0, 0, 60 }, { OP_KEYS, 0, 0, 0, 0, 200 }, { OP_END, 0, 0, 0, 0, 0 },
	// 8 refuel at a base after losses
	{ OP_FIND, TERRAIN_ROAD, TERRAIN_ROAD, 1, 0, 0 },
	{ OP_STOCK, 20, 30, 35, 0, 0 }, { OP_KEYS, 0, 0, 0, 0, 400 }, { OP_END, 0, 0, 0, 0, 0 },
	// 9 capture a neutral base
	{ OP_BASE, 0, 0, 0, 0, 0 }, { OP_KEYS, 0, 0, 0, 0, 1 }, { OP_KEYS, 0, 0, 0, 0, 20 },
	{ OP_END, 0, 0, 0, 0, 0 },
	// 10 refuel at the captured base
	{ OP_BASE, 0, 0, 0, 0, 0 }, { OP_KEYS, 0, 0, 0, 0, 1 }, { OP_STOCK, 20, 30, 35, 0, 0 },
	{ OP_KEYS, 0, 0, 0, 0, 100 }, { OP_KEYS, 0, 0, 0, 0, 500 }, { OP_END, 0, 0, 0, 0, 0 },
	// 11 shot by a pill: still, then driving round
	{ OP_NEARPILL, 5, 0, 0, 0, 0 }, { OP_KEYS, 0, 0, 0, 0, 1 }, { OP_KEYS, 0, 0, 0, 0, 100 },
	{ OP_KEYS, 0, 0, 0, 0, 150 }, { OP_KEYS, F | IN_LEFT, 0, 0, 0, 200 },
	{ OP_KEYS, 0, 0, 0, 0, 400 }, { OP_END, 0, 0, 0, 0, 0 },
	// 12 shoot a pill down and pick it up
	{ OP_NEARPILL, 3, 0, 0, 0, 0 }, { OP_PILLARM, 2, 0, 0, 0, 0 },
	{ OP_KEYS, IN_SIGHT_NEAR, 0, 0, 0, 7 }, { OP_KEYS, IN_FIRE, 0, 0, 0, 40 },
	{ OP_KEYS, F, 0, 0, 0, 60 }, { OP_KEYS, IN_SLOWER, 0, 0, 0, 20 }, { OP_END, 0, 0, 0, 0, 0 },
	// 13 base restocking over time
	{ OP_BASE, 0, 0, 0, 0, 0 }, { OP_KEYS, 0, 0, 0, 0, 1 }, { OP_BASESTOCK, 30, 20, 10, 0, 0 },
	{ OP_AWAY, 1, 0, 0, 0, 0 }, { OP_KEYS, 0, 0, 0, 0, 998 }, { OP_KEYS, 0, 0, 0, 0, 2002 },
	{ OP_END, 0, 0, 0, 0, 0 },
	// 14 what each game type starts with
	{ OP_TYPE, GAME_OPEN, 0, 0, 0, 0 }, { OP_TYPE, GAME_TOURNAMENT, 0, 0, 0, 0 },
	{ OP_TYPE, GAME_STRICT, 0, 0, 0, 0 }, { OP_BASE, 0, 0, 0, 0, 0 },
	{ OP_KEYS, 0, 0, 0, 0, 1 }, { OP_TYPE, GAME_TOURNAMENT, 1, 0, 0, 0 },
	{ OP_END, 0, 0, 0, 0, 0 },
	// 15 gather trees from forest two squares east
	{ OP_FIND, TERRAIN_GRASS, TERRAIN_FOREST, 2, 0, 0 }, { OP_TREES, 0, 0, 0, 0, 0 },
	{ OP_BUILD, BUILD_TREE, 2, 0, 0, 40 }, { OP_KEYS, 0, 0, 0, 0, 160 },
	{ OP_END, 0, 0, 0, 0, 0 },
	// 16 a road, then one on the road (refused)
	{ OP_FIND, TERRAIN_GRASS, TERRAIN_GRASS, 3, 0, 0 }, { OP_TREES, 10, 0, 0, 0, 0 },
	{ OP_BUILD, BUILD_ROAD, 2, 0, 0, 200 }, { OP_BUILD, BUILD_ROAD, 2, 0, 0, 5 },
	{ OP_END, 0, 0, 0, 0, 0 },
	// 17 a wall, repairing a damaged one, one on the tank's square (refused)
	{ OP_FIND, TERRAIN_GRASS, TERRAIN_GRASS, 3, 0, 0 }, { OP_TREES, 10, 0, 0, 0, 0 },
	{ OP_BUILD, BUILD_WALL, 2, 0, 0, 200 }, { OP_SET, TERRAIN_HALFBUILDING, 2, 0, 0, 0 },
	{ OP_BUILD, BUILD_WALL, 2, 0, 0, 200 }, { OP_BUILD, BUILD_WALL, 0, 0, 0, 5 },
	{ OP_END, 0, 0, 0, 0, 0 },
	// 18 a wall on a river makes a boat
	{ OP_FIND, TERRAIN_GRASS, TERRAIN_RIVER, 2, 0, 0 }, { OP_TREES, 30, 0, 0, 0, 0 },
	{ OP_BUILD, BUILD_WALL, 2, 0, 0, 200 }, { OP_END, 0, 0, 0, 0, 0 },
	// 19 put down a carried pill
	{ OP_NEARPILL, 4, 0, 0, 0, 0 }, { OP_CARRY, 0, 0, 0, 0, 0 }, { OP_TREES, 10, 0, 0, 0, 0 },
	{ OP_BUILD, BUILD_PILL, 2, 0, 0, 250 }, { OP_END, 0, 0, 0, 0, 0 },
	// 20 repair an own pill
	{ OP_NEARPILL, 3, 0, 0, 0, 0 }, { OP_PILLOWN, 0, 0, 0, 0, 0 }, { OP_PILLARM, 5, 0, 0, 0, 0 },
	{ OP_TREES, 10, 0, 0, 0, 0 }, { OP_BUILD, BUILD_PILL, 3, 0, 0, 250 },
	{ OP_END, 0, 0, 0, 0, 0 },
	// 21 lay a mine, then a road on it (refused)
	{ OP_FIND, TERRAIN_GRASS, TERRAIN_GRASS, 3, 0, 0 }, { OP_BUILD, BUILD_MINE, 2, 0, 0, 200 },
	{ OP_BUILD, BUILD_ROAD, 2, 0, 0, 5 }, { OP_END, 0, 0, 0, 0, 0 },
	// 22 the builder is shot on his way to build, and a new one flies in
	{ OP_FIND, TERRAIN_GRASS, TERRAIN_FOREST, 2, 0, 0 }, { OP_TREES, 10, 0, 0, 0, 0 },
	{ OP_BUILD, BUILD_TREE, 2, 0, 0, 1 }, { OP_KEYS, IN_SIGHT_NEAR, 0, 0, 0, 10 },
	{ OP_KEYS, IN_FIRE, 0, 0, 0, 50 }, { OP_KEYS, 0, 0, 0, 0, 1000 }, { OP_KEYS, 0, 0, 0, 0, 1000 },
	{ OP_END, 0, 0, 0, 0, 0 },
	// 23 trees growing back
	{ OP_FIND, TERRAIN_GRASS, TERRAIN_FOREST, 2, 0, 0 }, { OP_KEYS, 0, 0, 0, 0, 5000 },
	{ OP_KEYS, 0, 0, 0, 0, 5000 }, { OP_KEYS, 0, 0, 0, 0, 5000 }, { OP_KEYS, 0, 0, 0, 0, 5000 },
	{ OP_END, 0, 0, 0, 0, 0 },
	// 24 a mine by a river is shot, and its crater floods
	{ OP_FIND, TERRAIN_GRASS, TERRAIN_GRASS, 3, 0, 0 }, { OP_KEYS, IN_MINE, 0, 0, 0, 1 },
	{ OP_MOVE, 0, 2, 0, 0, 0 }, { OP_FACE, 12, 0, 0, 0, 0 },
	{ OP_KEYS, IN_SIGHT_NEAR, 0, 0, 0, 10 }, { OP_KEYS, IN_FIRE, 0, 0, 0, 1 },
	{ OP_KEYS, 0, 0, 0, 0, 30 }, { OP_KEYS, 0, 0, 0, 0, 40 }, { OP_END, 0, 0, 0, 0, 0 },
	// 25 a small wreck ends on a boat and craters it, the crater floods; a
	// big wreck beside a boat leaves it
	{ OP_FIND, TERRAIN_GRASS, TERRAIN_RIVER, 2, 0, 0 }, { OP_SET, TERRAIN_BOAT, 2, 0, 0, 0 },
	{ OP_WRECK, 0, 2, 0, 0, 0 }, { OP_KEYS, 0, 0, 0, 0, 4 }, { OP_KEYS, 0, 0, 0, 0, 100 },
	{ OP_SET, TERRAIN_BOAT, 2, 0, 0, 0 }, { OP_WRECK, 1, 1, 0, 0, 0 },
	{ OP_KEYS, 0, 0, 0, 0, 4 }, { OP_KEYS, 0, 0, 0, 0, 100 }, { OP_END, 0, 0, 0, 0, 0 },
	// 26 a mine kills a tank with no stock, the wreck is big anyway
	{ OP_FIND, TERRAIN_GRASS, TERRAIN_GRASS, 6, 0, 0 }, { OP_KEYS, IN_MINE, 0, 0, 0, 1 },
	{ OP_BACK, 1, 0, 0, 0, 0 }, { OP_STOCK, 1, 0, 0, 0, 0 }, { OP_KEYS, F, 0, 0, 0, 80 },
	{ OP_KEYS, 0, 0, 0, 0, 60 }, { OP_END, 0, 0, 0, 0, 0 },
	// 27 a wreck heading east onto its own player's armoured base stops
	// short of it
	{ OP_BASE, 0, 0, 0, 0, 0 }, { OP_KEYS, 0, 0, 0, 0, 1 }, { OP_BASESTOCK, 90, 0, 0, 0, 0 },
	{ OP_SET, TERRAIN_GRASS, 0xFF, 0, 0, 0 }, { OP_SET, TERRAIN_GRASS, 0xFE, 0, 0, 0 },
	{ OP_WRECK, 0, 0xFE, 0, 0, 12 },
	{ OP_KEYS, 0, 0, 0, 0, 4 }, { OP_KEYS, 0, 0, 0, 0, 40 }, { OP_END, 0, 0, 0, 0, 0 }
};
#undef F

static const char kNames[] =
	"drive\0speeds\0boat\0wading\0deep sea\0shoot\0range\0mines\0refuel\0"
	"capture\0base fuel\0pill shot\0pill down\0restock\0types\0trees\0road\0wall\0"
	"boat wall\0put pill\0fix pill\0lgm mine\0lgm shot\0regrow\0flood\0boat wreck\0"
	"mine wreck\0base wreck\0";

const char *script_name(uint8_t n)
{
	const char *p = kNames;

	while (n--)
		while (*p++)
			;
	return p;
}

// For the refuel script the square must be a base; for the rest, terrain.
static uint8_t find(GameState *s, const Op *o)
{
	Tank *k = &s->tanks[0];
	uint8_t x, y, i, ok;

	for (y = MAP_EDGE_LOW + 1; y < MAP_EDGE_HIGH; y++) {
		for (x = MAP_EDGE_LOW + 1; x + o->c < MAP_EDGE_HIGH; x++) {
			if (o->c == 1 && o->a == TERRAIN_ROAD && o->b == TERRAIN_ROAD) {
				if (g_base_at(s, x, y) < 0)
					continue;
			} else {
				if (g_base_at(s, x, y) >= 0 || g_pill_at(s, x, y) >= 0)
					continue;
				ok = game_terrain((uint8_t)(x + o->c), y) == o->b;
				for (i = 0; i < o->c && ok; i++)
					ok = game_terrain((uint8_t)(x + i), y) == o->a &&
						g_pill_at(s, (uint8_t)(x + i), y) < 0 &&
						g_base_at(s, (uint8_t)(x + i), y) < 0;
				if (!ok)
					continue;
			}
			k->x = (uint16_t)(((uint16_t)x << 8) + 128U);
			k->y = (uint16_t)(((uint16_t)y << 8) + 128U);
			k->angle = 64 * ANGLE_ONE;
			k->speed = 0;
			k->residual = 0;
			k->boat = o->d ? BOAT_IN : BOAT_OFF;
			k->lastRiverX = x;
			k->lastRiverY = y;
			return 1;
		}
	}
	return 0;
}

static void place(Tank *k, uint8_t x, uint8_t y)
{
	k->x = (uint16_t)(((uint16_t)x << 8) + 128U);
	k->y = (uint16_t)(((uint16_t)y << 8) + 128U);
	k->angle = 64 * ANGLE_ONE;
	k->speed = 0;
	k->residual = 0;
	k->boat = BOAT_OFF;
}

// The first base with no live pill within 9 squares.
static int8_t find_base(GameState *s)
{
	uint8_t i, j, ok;

	for (i = 0; i < s->map.numBases; i++) {
		const MapBase *b = &s->map.bases[i];

		ok = b->active;
		for (j = 0; j < s->map.numPills && ok; j++) {
			const MapPill *p = &s->map.pills[j];
			uint8_t dx = p->x > b->x ? p->x - b->x : b->x - p->x;
			uint8_t dy = p->y > b->y ? p->y - b->y : b->y - p->y;

			if (p->armour > 0 && dx <= 9 && dy <= 9)
				ok = 0;
		}
		if (ok)
			return (int8_t)i;
	}
	return -1;
}

static uint8_t open_ground(GameState *s, uint8_t x, uint8_t y)
{
	uint8_t t = game_terrain(x, y);

	return (t == TERRAIN_GRASS || t == TERRAIN_ROAD) && g_pill_at(s, x, y) < 0 &&
		g_base_at(s, x, y) < 0;
}

// The first pill with open ground from d squares west of it up to it.
static int8_t find_pill(GameState *s, uint8_t d)
{
	uint8_t i, j, ok;

	for (i = 0; i < s->map.numPills; i++) {
		const MapPill *p = &s->map.pills[i];

		ok = p->active && p->x > d;
		for (j = 1; j <= d && ok; j++)
			ok = open_ground(s, (uint8_t)(p->x - j), p->y);
		if (ok)
			return (int8_t)i;
	}
	return -1;
}

uint32_t script_run(GameState *s, uint8_t n,
	void (*step)(const GameState *s, uint8_t n, uint8_t i, void *ctx), void *ctx)
{
	const Op *o = kScripts;
	uint32_t crc = 0xFFFFFFFFUL, v;
	uint8_t i = 0, b[8];
	uint16_t t;
	uint8_t n0 = n;
	int8_t pill = -1, base = -1;
	GameInput in;
	GameSetup setup;
	MapInfo start = s->map;

	while (n) {
		if (o->op == OP_END)
			n--;
		o++;
	}
	game_init(s, 4242, 0);
	in.opts = 0;
	in.build = BUILD_NONE;
	in.buildX = 0;
	in.buildY = 0;
	for (; o->op != OP_END; o++, i++) {
		Tank *k = &s->tanks[0];

		switch (o->op) {
		case OP_KEYS:
			for (t = 0; t < o->n; t++) {
				in.keys = o->a;
				game_tick(s, &in);
			}
			break;
		case OP_BUILD:
			in.keys = 0;
			in.build = o->a;
			in.buildX = (uint8_t)((k->x >> 8) + (int8_t)o->b);
			in.buildY = (uint8_t)((k->y >> 8) + (int8_t)o->c);
			for (t = 0; t < o->n; t++) {
				game_tick(s, &in);
				in.build = BUILD_NONE;
			}
			break;
		case OP_TREES:
			k->trees = o->a;
			break;
		case OP_SET:
			game_set_terrain((uint8_t)((k->x >> 8) + (int8_t)o->b),
				(uint8_t)((k->y >> 8) + (int8_t)o->c), o->a);
			break;
		case OP_WRECK:
			g_tank_explosion(s, (uint16_t)(k->x + (int16_t)((int8_t)o->b * 256)),
				(uint16_t)(k->y + (int16_t)((int8_t)o->c * 256)), k->angle,
				(uint8_t)o->n, o->a, k->player);
			break;
		case OP_MOVE:
			k->x = (uint16_t)(k->x + (int16_t)((int8_t)o->b * 256));
			k->y = (uint16_t)(k->y + (int16_t)((int8_t)o->c * 256));
			break;
		case OP_CARRY:
			if (pill >= 0 && k->numCarry < MAP_MAX_PILLS) {
				s->map.pills[pill].inTank = 1;
				s->map.pills[pill].owner = k->player;
				for (t = k->numCarry; t > 0; t--)
					k->carry[t] = k->carry[t - 1];
				k->carry[0] = (uint8_t)pill;
				k->numCarry++;
			}
			break;
		case OP_PILLOWN:
			if (pill >= 0)
				s->map.pills[pill].owner = o->a;
			break;
		case OP_FIND:
			b[0] = find(s, o);
			crc = util_crc(crc, b, 1);
			break;
		case OP_STOCK:
			k->armour = o->a;
			k->shells = o->b;
			k->mines = o->c;
			break;
		case OP_FACE:
			k->angle = (uint16_t)(o->a * 16 * ANGLE_ONE);
			break;
		case OP_BACK:
			k->x = (uint16_t)(k->x - ((uint16_t)o->a << 8));
			break;
		case OP_AWAY:
			k->y = (uint16_t)(k->y + ((uint16_t)o->a << 8));
			break;
		case OP_BASE:
			base = find_base(s);
			b[0] = (uint8_t)base;
			crc = util_crc(crc, b, 1);
			if (base >= 0)
				place(k, s->map.bases[base].x, s->map.bases[base].y);
			break;
		case OP_NEARPILL:
			pill = find_pill(s, o->a);
			b[0] = (uint8_t)pill;
			crc = util_crc(crc, b, 1);
			if (pill >= 0)
				place(k, (uint8_t)(s->map.pills[pill].x - o->a), s->map.pills[pill].y);
			break;
		case OP_PILLARM:
			if (pill >= 0)
				s->map.pills[pill].armour = o->a;
			break;
		case OP_BASESTOCK:
			if (base >= 0) {
				s->map.bases[base].armour = o->a;
				s->map.bases[base].shells = o->b;
				s->map.bases[base].mines = o->c;
			}
			break;
		case OP_TYPE:
			// Keeps the bases as they are now when b is set, else the
			// map's.
			if (!o->b)
				s->map = start;
			for (t = 0; t < sizeof(setup); t++)
				((uint8_t *)&setup)[t] = 0;
			setup.gameType = o->a;
			setup.numPlayers = 1;
			game_init(s, 4242, &setup);
			break;
		}
		if (step)
			step(s, n0, i, ctx);
		v = game_checksum(s);
		b[0] = (uint8_t)(v >> 24); b[1] = (uint8_t)(v >> 16);
		b[2] = (uint8_t)(v >> 8); b[3] = (uint8_t)v;
		v = game_terrain_checksum();
		b[4] = (uint8_t)(v >> 24); b[5] = (uint8_t)(v >> 16);
		b[6] = (uint8_t)(v >> 8); b[7] = (uint8_t)v;
		crc = util_crc(crc, b, 8);
	}
	return ~crc;
}
