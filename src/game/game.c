// PalmBolo game rules: the tick, terrain changes, mines, flooding and
// explosions. The tank is in tank.c and shells in shells.c.
//
// Ported from WinBolo 2 (src/bolo/), Copyright (c) 1998-2026 John Morrison,
// GPLv3. This version Copyright (c) 2026 the PalmBolo authors, GPLv3.
#include "game.h"
#include "gamepriv.h"
#include "mapstore.h"
#include "util.h"

// Squares changed since the view last asked, outside the game state.
#define CHANGE_MAX 32
static uint8_t gChange[CHANGE_MAX * 2];
static uint8_t gChangeCount, gChangeOver;

uint8_t game_terrain(uint8_t x, uint8_t y)
{
	if (x > MAP_EDGE_LOW && x < MAP_EDGE_HIGH && y > MAP_EDGE_LOW && y < MAP_EDGE_HIGH)
		return mapstore_get(x, y);
	return TERRAIN_DEEP_SEA;
}

void game_set_terrain(uint8_t x, uint8_t y, uint8_t t)
{
	if (mapstore_get(x, y) == t)
		return;
	mapstore_set(x, y, t);
	if (gChangeCount < CHANGE_MAX) {
		gChange[gChangeCount * 2] = x;
		gChange[gChangeCount * 2 + 1] = y;
		gChangeCount++;
	} else {
		gChangeOver = 1;
	}
}

uint8_t game_take_changes(uint8_t *xy, uint8_t max)
{
	uint8_t i, n = gChangeCount;

	if (gChangeOver || n > max) {
		gChangeCount = 0;
		gChangeOver = 0;
		return 255;
	}
	for (i = 0; i < n * 2; i++)
		xy[i] = gChange[i];
	gChangeCount = 0;
	return n;
}

uint8_t g_is_mine(uint8_t x, uint8_t y)
{
	uint8_t t;

	if (x <= MAP_EDGE_LOW || x >= MAP_EDGE_HIGH || y <= MAP_EDGE_LOW || y >= MAP_EDGE_HIGH)
		return 1;
	t = mapstore_get(x, y);
	return t >= TERRAIN_MINE_FIRST && t <= TERRAIN_MINE_LAST;
}

int8_t g_base_at(const GameState *s, uint8_t x, uint8_t y)
{
	uint8_t i;

	for (i = 0; i < s->map.numBases; i++)
		if (s->map.bases[i].x == x && s->map.bases[i].y == y && s->map.bases[i].active)
			return (int8_t)i;
	return -1;
}

int8_t g_pill_at(const GameState *s, uint8_t x, uint8_t y)
{
	uint8_t i;

	for (i = 0; i < s->map.numPills; i++) {
		const MapPill *p = &s->map.pills[i];

		if (p->x == x && p->y == y && p->active && !p->inTank)
			return (int8_t)i;
	}
	return -1;
}

int8_t g_live_pill_at(const GameState *s, uint8_t x, uint8_t y)
{
	int8_t p = g_pill_at(s, x, y);

	return p >= 0 && s->map.pills[p].armour > 0 ? p : -1;
}

uint8_t game_allied(const GameState *s, uint8_t a, uint8_t b)
{
	if (a == b)
		return 1;
	if (a >= GAME_MAX_PLAYERS || b >= GAME_MAX_PLAYERS || !s->players[a].inUse)
		return 0;
	return (uint8_t)((s->players[a].allies >> b) & 1);
}

uint8_t game_owner_kind(const GameState *s, uint8_t owner, uint8_t viewer)
{
	if (owner >= GAME_MAX_PLAYERS)
		return OWN_NEUTRAL;
	if (owner == viewer)
		return OWN_SELF;
	return game_allied(s, owner, viewer) ? OWN_ALLY : OWN_ENEMY;
}

uint8_t g_is_land(const GameState *s, uint8_t x, uint8_t y)
{
	uint8_t t = mapstore_get(x, y);

	if ((t == TERRAIN_RIVER || t == TERRAIN_DEEP_SEA) && g_base_at(s, x, y) < 0)
		return 0;
	return 1;
}

// Explosions: drawn only, but kept in the state so the view and the
// checksum agree. The newest is first.
void g_explosion(GameState *s, uint16_t wx, uint16_t wy)
{
	uint8_t i = s->numExpl < GAME_MAX_EXPL ? s->numExpl : GAME_MAX_EXPL - 1;

	for (; i > 0; i--)
		s->expl[i] = s->expl[i - 1];
	s->expl[0].x = wx;
	s->expl[0].y = wy;
	s->expl[0].frame = R_EXPLOSION_START;
	if (s->numExpl < GAME_MAX_EXPL)
		s->numExpl++;
}

static void explosions_update(GameState *s)
{
	uint8_t i, n = 0;

	if (++s->explTimer < R_EXPLOSION_TICKS)
		return;
	s->explTimer = 0;
	for (i = 0; i < s->numExpl; i++) {
		if (s->expl[i].frame <= 1)
			continue;
		s->expl[n] = s->expl[i];
		s->expl[n].frame--;
		n++;
	}
	s->numExpl = n;
}

// Damage lists (WinBolo's building.c, grass.c, swamp.c, rubble.c). The
// first hit adds the square with its full life and returns the hit terrain;
// each later hit takes one off, and at 0 the square becomes dead.
uint8_t g_damage(GameState *s, uint8_t kind, uint8_t x, uint8_t y)
{
	static const uint8_t kLife[DMG_KINDS] = {
		R_BUILDING_LIFE, R_GRASS_LIFE, R_SWAMP_LIFE, R_RUBBLE_LIFE
	};
	static const uint8_t kHit[DMG_KINDS] = {
		TERRAIN_HALFBUILDING, TERRAIN_GRASS, TERRAIN_SWAMP, TERRAIN_RUBBLE
	};
	static const uint8_t kDead[DMG_KINDS] = {
		TERRAIN_RUBBLE, TERRAIN_SWAMP, TERRAIN_RIVER, TERRAIN_RIVER
	};
	SquareList *l = &s->damage[kind];
	uint8_t i;

	for (i = 0; i < l->count; i++) {
		if (l->item[i].x != x || l->item[i].y != y)
			continue;
		if (--l->item[i].n == 0) {
			for (; i + 1 < l->count; i++)
				l->item[i] = l->item[i + 1];
			l->count--;
			return kDead[kind];
		}
		return kHit[kind];
	}
	// Full list: the oldest square forgets its damage.
	i = l->count < GAME_MAX_DAMAGE ? l->count : GAME_MAX_DAMAGE - 1;
	for (; i > 0; i--)
		l->item[i] = l->item[i - 1];
	l->item[0].x = x;
	l->item[0].y = y;
	l->item[0].n = kLife[kind];
	if (l->count < GAME_MAX_DAMAGE)
		l->count++;
	return kHit[kind];
}

void g_damage_remove(GameState *s, uint8_t kind, uint8_t x, uint8_t y)
{
	SquareList *l = &s->damage[kind];
	uint8_t i;

	for (i = 0; i < l->count; i++) {
		if (l->item[i].x == x && l->item[i].y == y) {
			for (; i + 1 < l->count; i++)
				l->item[i] = l->item[i + 1];
			l->count--;
			return;
		}
	}
}

// floodAddItem: appended at the end, filled when its time runs out.
void g_flood(GameState *s, uint8_t x, uint8_t y)
{
	uint8_t i;

	for (i = 0; i < s->numFlood; i++)
		if (s->flood[i].x == x && s->flood[i].y == y)
			return;
	if (s->numFlood >= GAME_MAX_FLOOD)
		return;
	s->flood[s->numFlood].x = x;
	s->flood[s->numFlood].y = y;
	s->flood[s->numFlood].n = R_FLOOD_FILL_TICKS;
	s->numFlood++;
}

// floodCheckFill: a crater next to water becomes river, and its crater
// neighbours start to fill.
static void flood_fill(GameState *s, uint8_t x, uint8_t y)
{
	static const int8_t kDx[4] = { -1, 1, 0, 0 };
	static const int8_t kDy[4] = { 0, 0, -1, 1 };
	uint8_t i, t, wet = 0;

	if (g_base_at(s, x, y) >= 0 || g_pill_at(s, x, y) >= 0)
		return;
	for (i = 0; i < 4; i++) {
		uint8_t nx = (uint8_t)(x + kDx[i]), ny = (uint8_t)(y + kDy[i]);

		t = mapstore_get(nx, ny);
		if (g_base_at(s, nx, ny) >= 0 || g_pill_at(s, nx, ny) >= 0)
			t = TERRAIN_ROAD;
		if (t == TERRAIN_DEEP_SEA || t == TERRAIN_BOAT || t == TERRAIN_RIVER)
			wet = 1;
	}
	if (!wet)
		return;
	game_set_terrain(x, y, TERRAIN_RIVER);
	for (i = 0; i < 4; i++) {
		uint8_t nx = (uint8_t)(x + kDx[i]), ny = (uint8_t)(y + kDy[i]);

		t = mapstore_get(nx, ny);
		if (t == TERRAIN_CRATER || t == TERRAIN_MINE_CRATER)
			g_flood(s, nx, ny);
	}
}

// floodUpdate: every timer counts down, and the first one to run out is
// filled. WinBolo stops the pass there, so the rest wait a tick.
static void flood_update(GameState *s)
{
	uint8_t i, j;

	for (i = 0; i < s->numFlood; i++) {
		if (s->flood[i].n > 0)
			s->flood[i].n--;
		if (s->flood[i].n == 0) {
			uint8_t x = s->flood[i].x, y = s->flood[i].y;

			for (j = i; j + 1 < s->numFlood; j++)
				s->flood[j] = s->flood[j + 1];
			s->numFlood--;
			flood_fill(s, x, y);
			return;
		}
	}
}

// minesExpAddItem: a mine at this square goes off after the fuse. The
// newest is first, and a square already waiting isn't added again.
void g_mine_exp(GameState *s, uint8_t x, uint8_t y)
{
	uint8_t i;

	for (i = 0; i < s->numMineExp; i++)
		if (s->mineExp[i].x == x && s->mineExp[i].y == y)
			return;
	i = s->numMineExp < GAME_MAX_MINEEXP ? s->numMineExp : GAME_MAX_MINEEXP - 1;
	for (; i > 0; i--)
		s->mineExp[i] = s->mineExp[i - 1];
	s->mineExp[0].x = x;
	s->mineExp[0].y = y;
	s->mineExp[0].n = R_MINE_FUSE_TICKS;
	if (s->numMineExp < GAME_MAX_MINEEXP)
		s->numMineExp++;
}

// minesExpCheckFill: a mine here blows a crater, hurts tanks near it and
// sets off the mines beside it.
static void mine_fill(GameState *s, uint8_t x, uint8_t y)
{
	uint8_t i;

	if (x <= MAP_EDGE_LOW || x >= MAP_EDGE_HIGH || y <= MAP_EDGE_LOW || y >= MAP_EDGE_HIGH)
		return;
	if (!g_is_mine(x, y))
		return;
	game_set_terrain(x, y, TERRAIN_CRATER);
	for (i = 0; i < s->numTanks; i++)
		tank_mine_damage(s, i, x, y);
	g_explosion(s, (uint16_t)(((uint16_t)x << 8) + 128U), (uint16_t)(((uint16_t)y << 8) + 128U));
	g_flood(s, x, y);
	g_damage_remove(s, DMG_GRASS, x, y);
	g_damage_remove(s, DMG_SWAMP, x, y);
	g_damage_remove(s, DMG_RUBBLE, x, y);
	if (g_is_mine((uint8_t)(x - 1), y))
		g_mine_exp(s, (uint8_t)(x - 1), y);
	if (g_is_mine((uint8_t)(x + 1), y))
		g_mine_exp(s, (uint8_t)(x + 1), y);
	if (g_is_mine(x, (uint8_t)(y - 1)))
		g_mine_exp(s, x, (uint8_t)(y - 1));
	if (g_is_mine(x, (uint8_t)(y + 1)))
		g_mine_exp(s, x, (uint8_t)(y + 1));
}

// minesExpUpdate: as with flooding, the first one to run out goes off and
// the pass stops there.
static void mines_update(GameState *s)
{
	uint8_t i, j;

	for (i = 0; i < s->numMineExp; i++) {
		if (s->mineExp[i].n > 0)
			s->mineExp[i].n--;
		if (s->mineExp[i].n == 0) {
			uint8_t x = s->mineExp[i].x, y = s->mineExp[i].y;

			for (j = i; j + 1 < s->numMineExp; j++)
				s->mineExp[j] = s->mineExp[j + 1];
			s->numMineExp--;
			mine_fill(s, x, y);
			return;
		}
	}
}

// tkExplosionAddItem: the wreck of a dead tank.
void g_tank_explosion(GameState *s, uint16_t wx, uint16_t wy, uint16_t angle,
	uint8_t length, uint8_t big, uint8_t creator)
{
	uint8_t i = s->numTkExpl < GAME_MAX_TKEXPL ? s->numTkExpl : GAME_MAX_TKEXPL - 1;

	for (; i > 0; i--)
		s->tkExpl[i] = s->tkExpl[i - 1];
	s->tkExpl[0].x = wx;
	s->tkExpl[0].y = wy;
	s->tkExpl[0].angle = angle;
	s->tkExpl[0].length = length;
	s->tkExpl[0].big = big;
	s->tkExpl[0].creator = creator;
	if (s->numTkExpl < GAME_MAX_TKEXPL)
		s->numTkExpl++;
}

static uint8_t crater_ok(const GameState *s, uint8_t x, uint8_t y)
{
	uint8_t t = mapstore_get(x, y);

	return t != TERRAIN_BOAT && t != TERRAIN_RIVER && t != TERRAIN_DEEP_SEA &&
		g_base_at(s, x, y) < 0 && g_pill_at(s, x, y) < 0;
}

// A big wreck's square: a pill there is damaged, other squares crater.
static void big_square(GameState *s, uint8_t x, uint8_t y)
{
	uint8_t k;
	int8_t p = g_pill_at(s, x, y);

	for (k = 0; k < DMG_KINDS; k++)
		g_damage_remove(s, k, x, y);
	if (p >= 0) {
		pills_damage(s, (uint8_t)p, R_TANK_EXPLOSION_DAMAGE);
		return;
	}
	if (crater_ok(s, x, y)) {
		game_set_terrain(x, y, TERRAIN_CRATER);
		g_flood(s, x, y);
	}
}

// tkExplosionUpdate: every second tick each wreck moves on and leaves
// explosions behind it; at the end it blows a crater, or four.
static void tk_explosions_update(GameState *s)
{
	uint8_t i, n = 0;

	if (++s->tkExplTimer < R_TK_EXPLOSION_TICKS)
		return;
	s->tkExplTimer = 0;
	for (i = 0; i < s->numTkExpl; i++) {
		TankExplosion *e = &s->tkExpl[i];
		uint8_t mx, my, t, keep = 1;

		if (e->length > 0) {
			int16_t dx, dy;
			uint16_t nx, ny;

			util_distance(&dx, &dy, e->angle, R_TK_EXPLOSION_MOVE);
			g_explosion(s, e->x, e->y);
			nx = (uint16_t)(e->x + dx + (dx > 0 ? R_TK_EXPLOSION_WIDTH : -R_TK_EXPLOSION_WIDTH));
			ny = (uint16_t)(e->y + dy + (dy > 0 ? R_TK_EXPLOSION_HEIGHT : -R_TK_EXPLOSION_HEIGHT));
			if (g_speed_at(s, (uint8_t)(e->x >> 8), (uint8_t)(ny >> 8), 0, e->creator) > 0)
				e->y = (uint16_t)(e->y + dy);
			if (g_speed_at(s, (uint8_t)(nx >> 8), (uint8_t)(e->y >> 8), 0, e->creator) > 0)
				e->x = (uint16_t)(e->x + dx);
			e->length--;
			mx = (uint8_t)(e->x >> 8);
			my = (uint8_t)(e->y >> 8);
			t = game_terrain(mx, my);
			if (t == TERRAIN_DEEP_SEA)
				keep = 0;
			else if (t == TERRAIN_FOREST)
				game_set_terrain(mx, my, TERRAIN_GRASS);
			else if (t == TERRAIN_BOAT)
				game_set_terrain(mx, my, TERRAIN_RIVER);
		} else {
			mx = (uint8_t)(e->x >> 8);
			my = (uint8_t)(e->y >> 8);
			keep = 0;
			if (!e->big) {
				g_explosion(s, e->x, e->y);
				if (crater_ok(s, mx, my)) {
					game_set_terrain(mx, my, TERRAIN_CRATER);
					g_flood(s, mx, my);
				}
			} else {
				int16_t dx, dy;
				uint8_t ax, ay;

				util_distance(&dx, &dy, e->angle, R_TK_EXPLOSION_MOVE);
				ax = (uint8_t)(dx > 0 ? mx + 1 : mx - 1);
				ay = (uint8_t)(dy > 0 ? my + 1 : my - 1);
				g_explosion(s, e->x, e->y);
				big_square(s, ax, ay);
				big_square(s, ax, my);
				big_square(s, mx, ay);
				big_square(s, mx, my);
			}
		}
		if (keep)
			s->tkExpl[n++] = *e;
	}
	s->numTkExpl = n;
}

// mapClampToRules: map values past what the rules allow are cut back.
static void clamp_map(MapInfo *m)
{
	uint8_t i;

	for (i = 0; i < m->numPills; i++) {
		MapPill *p = &m->pills[i];

		if (p->armour > R_PILL_MAX_ARMOUR)
			p->armour = R_PILL_MAX_ARMOUR;
		if (p->speed < R_PILL_ATTACK_MIN)
			p->speed = R_PILL_ATTACK_MIN;
		if (p->speed > R_PILL_ATTACK_NORMAL)
			p->speed = R_PILL_ATTACK_NORMAL;
		p->coolDown = p->speed != R_PILL_ATTACK_NORMAL ? R_PILL_COOLDOWN : 0;
		p->reload = 0;
		p->justSeen = 0;
		p->inTank = 0;
	}
	for (i = 0; i < m->numBases; i++) {
		MapBase *b = &m->bases[i];

		if (b->armour > R_BASE_FULL_ARMOUR)
			b->armour = R_BASE_FULL_ARMOUR;
		if (b->shells > R_BASE_FULL_SHELLS)
			b->shells = R_BASE_FULL_SHELLS;
		if (b->mines > R_BASE_FULL_MINES)
			b->mines = R_BASE_FULL_MINES;
	}
}

void game_init(GameState *s, uint32_t seed, const GameSetup *setup)
{
	uint8_t *p = (uint8_t *)s;
	uint16_t i;
	uint8_t n = setup ? setup->numPlayers : 1;
	MapInfo keep = s->map;

	for (i = 0; i < sizeof(*s); i++)
		p[i] = 0;
	s->map = keep;
	clamp_map(&s->map);
	s->rng = seed ? seed : 1;
	if (n < 1)
		n = 1;
	if (n > GAME_MAX_TANKS)
		n = GAME_MAX_TANKS;
	if (setup) {
		s->gameType = setup->gameType;
		s->hiddenMines = setup->hiddenMines;
	}
	for (i = 0; i < s->map.numBases; i++)
		s->baseStopped[i] = 1;
	// Restock timers are spread over the players in the game.
	for (i = 0; i < GAME_MAX_PLAYERS; i++) {
		if (i < n) {
			s->players[i].inUse = 1;
			s->players[i].allies = setup ? setup->allies[i] : 0;
			s->baseTimer[i] = (uint16_t)((R_BASE_REGEN_TICKS * (i + 1)) / n);
		}
	}
	gChangeCount = 0;
	gChangeOver = 0;
	s->numTanks = n;
	for (i = 0; i < n; i++)
		tank_create(s, (uint8_t)i, (uint8_t)i);
}

// xorshift32
uint32_t game_random(GameState *s)
{
	uint32_t r = s->rng;

	r ^= r << 13;
	r ^= r >> 17;
	r ^= r << 5;
	s->rng = r;
	return r;
}

uint16_t game_random_below(GameState *s, uint16_t n)
{
	uint16_t lim, r;

	if (n <= 1)
		return 0;
	lim = (uint16_t)(65536UL - 65536UL % n);
	do
		r = (uint16_t)game_random(s);
	while (lim != 0 && r >= lim);
	return (uint16_t)(r % n);
}

// Order as WinBolo's server tick: tanks, pills, bases and refuelling,
// wrecks, shells, mines, explosions, flooding.
void game_tick(GameState *s, const GameInput *in)
{
	uint8_t i;

	for (i = 0; i < s->numTanks; i++)
		tank_update(s, i, &in[i]);
	pills_update(s);
	bases_update(s);
	tk_explosions_update(s);
	shells_update(s);
	mines_update(s);
	explosions_update(s);
	flood_update(s);
	s->tick++;
}

static uint32_t c8(uint32_t crc, uint8_t v)
{
	return util_crc(crc, &v, 1);
}

static uint32_t c16(uint32_t crc, uint16_t v)
{
	uint8_t b[2];

	b[0] = (uint8_t)(v >> 8);
	b[1] = (uint8_t)v;
	return util_crc(crc, b, 2);
}

static uint32_t c32(uint32_t crc, uint32_t v)
{
	crc = c16(crc, (uint16_t)(v >> 16));
	return c16(crc, (uint16_t)v);
}

static uint32_t c_squares(uint32_t crc, const Square *q, uint8_t n)
{
	uint8_t i;

	crc = c8(crc, n);
	for (i = 0; i < n; i++) {
		crc = c8(crc, q[i].x);
		crc = c8(crc, q[i].y);
		crc = c8(crc, q[i].n);
	}
	return crc;
}

uint32_t game_checksum(const GameState *s)
{
	uint32_t crc = 0xFFFFFFFFUL;
	uint8_t i;

	crc = c32(crc, s->rng);
	crc = c32(crc, s->tick);
	crc = c8(crc, s->explTimer);
	crc = c8(crc, s->tkExplTimer);
	crc = c8(crc, s->halfShell);
	crc = c8(crc, s->halfMine);
	crc = c8(crc, s->gameType);
	crc = c8(crc, s->hiddenMines);
	for (i = 0; i < GAME_MAX_PLAYERS; i++) {
		crc = c8(crc, s->players[i].inUse);
		crc = c16(crc, s->players[i].allies);
		crc = c16(crc, s->baseTimer[i]);
	}
	for (i = 0; i < s->numTanks; i++) {
		const Tank *k = &s->tanks[i];

		crc = c16(crc, k->x);
		crc = c16(crc, k->y);
		crc = c16(crc, k->angle);
		crc = c16(crc, (uint16_t)k->speed);
		crc = c32(crc, (uint32_t)k->bumpX);
		crc = c32(crc, (uint32_t)k->bumpY);
		crc = c16(crc, k->deathWait);
		crc = c16(crc, k->deaths);
		crc = util_crc(crc, &k->armour, 20);
		crc = util_crc(crc, k->carry, k->numCarry);
	}
	crc = c8(crc, s->numShells);
	for (i = 0; i < s->numShells; i++) {
		const Shell *h = &s->shells[i];

		crc = c16(crc, h->x);
		crc = c16(crc, h->y);
		crc = c16(crc, h->angle);
		crc = c16(crc, (uint16_t)h->stepX);
		crc = c16(crc, (uint16_t)h->stepY);
		crc = c16(crc, (uint16_t)h->accX);
		crc = c16(crc, (uint16_t)h->accY);
		crc = c8(crc, h->length);
		crc = c8(crc, h->owner);
		crc = c8(crc, h->onBoat);
	}
	crc = c8(crc, s->numExpl);
	for (i = 0; i < s->numExpl; i++) {
		crc = c16(crc, s->expl[i].x);
		crc = c16(crc, s->expl[i].y);
		crc = c8(crc, s->expl[i].frame);
	}
	crc = c8(crc, s->numTkExpl);
	for (i = 0; i < s->numTkExpl; i++) {
		const TankExplosion *e = &s->tkExpl[i];

		crc = c16(crc, e->x);
		crc = c16(crc, e->y);
		crc = c16(crc, e->angle);
		crc = c8(crc, e->length);
		crc = c8(crc, e->big);
		crc = c8(crc, e->creator);
	}
	crc = c_squares(crc, s->mineExp, s->numMineExp);
	crc = c_squares(crc, s->flood, s->numFlood);
	for (i = 0; i < DMG_KINDS; i++)
		crc = c_squares(crc, s->damage[i].item, s->damage[i].count);
	for (i = 0; i < s->map.numBases; i++) {
		const MapBase *b = &s->map.bases[i];

		crc = util_crc(crc, &b->x, 7);
		crc = c8(crc, s->baseTime[i]);
		crc = c8(crc, s->baseStopped[i]);
	}
	for (i = 0; i < s->map.numPills; i++)
		crc = util_crc(crc, &s->map.pills[i].x, 10);
	return ~crc;
}

uint32_t game_terrain_checksum(void)
{
	uint32_t crc = 0xFFFFFFFFUL;
	uint16_t y;

	for (y = 0; y < 256; y++)
		crc = util_crc(crc, mapstore_row((uint8_t)y), 256);
	return ~crc;
}
