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
	OP_FACE     // turn the tank to face direction a (0-15)
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
	{ OP_STOCK, 20, 30, 35, 0, 0 }, { OP_KEYS, 0, 0, 0, 0, 400 }, { OP_END, 0, 0, 0, 0, 0 }
};
#undef F

static const char kNames[] =
	"drive\0speeds\0boat\0wading\0deep sea\0shoot\0range\0mines\0refuel\0";

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

uint32_t script_run(GameState *s, uint8_t n,
	void (*step)(const GameState *s, uint8_t n, uint8_t i, void *ctx), void *ctx)
{
	const Op *o = kScripts;
	uint32_t crc = 0xFFFFFFFFUL, v;
	uint8_t i = 0, b[8];
	uint16_t t;
	uint8_t n0 = n;
	GameInput in;

	while (n) {
		if (o->op == OP_END)
			n--;
		o++;
	}
	game_init(s, 4242);
	in.opts = 0;
	for (; o->op != OP_END; o++, i++) {
		Tank *k = &s->tanks[0];

		switch (o->op) {
		case OP_KEYS:
			for (t = 0; t < o->n; t++) {
				in.keys = o->a;
				game_tick(s, &in);
			}
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
