// The tank: turning, speed, movement over terrain, boats, mines, death and
// respawn.
//
// Ported from WinBolo 2 (src/bolo/tank.c), Copyright (c) 1998-2026 John
// Morrison, GPLv3. This version Copyright (c) 2026 the PalmBolo authors,
// GPLv3.
#include "gamepriv.h"
#include "mapstore.h"
#include "util.h"

// mapGetSpeed and mapGetTurnRate by terrain 0..15; deep sea is separate.
// On a boat every square counts as boat.
static const uint8_t kSpeed[16] = {
	R_SPEED_SOLID, R_SPEED_RIVER, R_SPEED_SWAMP, R_SPEED_CRATER,
	R_SPEED_ROAD, R_SPEED_FOREST, R_SPEED_RUBBLE, R_SPEED_GRASS,
	R_SPEED_SOLID, R_SPEED_BOAT, R_SPEED_SWAMP, R_SPEED_CRATER,
	R_SPEED_ROAD, R_SPEED_FOREST, R_SPEED_RUBBLE, R_SPEED_GRASS
};
static const uint8_t kTurn[16] = {
	R_TURN_SOLID, R_TURN_RIVER, R_TURN_SWAMP, R_TURN_CRATER,
	R_TURN_ROAD, R_TURN_FOREST, R_TURN_RUBBLE, R_TURN_GRASS,
	R_TURN_SOLID, R_TURN_BOAT, R_TURN_SWAMP, R_TURN_CRATER,
	R_TURN_ROAD, R_TURN_FOREST, R_TURN_RUBBLE, R_TURN_GRASS
};

uint8_t g_speed_at(const GameState *s, uint8_t x, uint8_t y, uint8_t onBoat, uint8_t player)
{
	int8_t b;
	uint8_t t;

	if (g_live_pill_at(s, x, y) >= 0)
		return R_SPEED_SOLID;
	b = g_base_at(s, x, y);
	if (b >= 0)
		return bases_cant_drive(s, (uint8_t)b, player) ? R_SPEED_SOLID : R_SPEED_BASE;
	if (onBoat)
		return R_SPEED_BOAT;
	t = game_terrain(x, y);
	return t < 16 ? kSpeed[t] : R_SPEED_DEEP_SEA;
}

static uint8_t turn_at(const GameState *s, uint8_t x, uint8_t y, uint8_t onBoat, uint8_t player)
{
	int8_t b;
	uint8_t t;

	if (g_live_pill_at(s, x, y) >= 0)
		return R_TURN_SOLID;
	b = g_base_at(s, x, y);
	if (b >= 0)
		return bases_cant_drive(s, (uint8_t)b, player) ? R_TURN_SOLID : R_TURN_BASE;
	if (onBoat)
		return R_TURN_BOAT;
	t = game_terrain(x, y);
	return t < 16 ? kTurn[t] : R_TURN_DEEP_SEA;
}

// Solid for a tank's circle: buildings, live pills and bases it can't
// drive over.
static uint8_t solid(const GameState *s, uint8_t x, uint8_t y, uint8_t player)
{
	int8_t b;
	uint8_t t;

	if (g_live_pill_at(s, x, y) >= 0)
		return 1;
	b = g_base_at(s, x, y);
	if (b >= 0)
		return bases_cant_drive(s, (uint8_t)b, player);
	t = game_terrain(x, y);
	return t == TERRAIN_BUILDING || t == TERRAIN_HALFBUILDING;
}

// tankGetGunsightAt as a 16-world-unit cell: 4 * sightLen shell steps
// (WinBolo's per-tick loop sums to floor(n * step / 256)) plus one more
// whole step, less half a square.
static uint16_t sight_cell(const Tank *k, uint16_t angle, uint16_t *cy)
{
	int16_t sx, sy, lx, ly;
	uint16_t x, y;
	uint8_t n = (uint8_t)(k->sightLen * (R_SHELL_LIFE / 2));

	util_shell_step(&sx, &sy, angle);
	util_distance(&lx, &ly, angle, R_SHELL_SPEED);
	x = (uint16_t)(k->x + (int16_t)(MUL16(sx, n) >> 8) + lx - 128);
	y = (uint16_t)(k->y + (int16_t)(MUL16(sy, n) >> 8) + ly - 128);
	*cy = (uint16_t)(y >> 4);
	return (uint16_t)(x >> 4);
}

void game_gunsight(const GameState *s, uint8_t t, uint16_t *wx, uint16_t *wy)
{
	uint16_t cy, cx = sight_cell(&s->tanks[t], s->tanks[t].angle, &cy);

	*wx = (uint16_t)(cx << 4);
	*wy = (uint16_t)(cy << 4);
}

// tankVisibleTurn: on the first tick of a turn, the smallest multiple of
// the step (up to 16 bradians) that moves the gunsight by a pixel.
static uint16_t visible_turn(const Tank *k, uint16_t step, uint8_t left)
{
	uint16_t cx, cy, nx, ny, amount, i;
	int16_t a;

	if (step == 0 || k->destroyed)
		return step;
	cx = sight_cell(k, k->angle, &cy);
	for (i = 1, amount = step; i <= 128; i++, amount = (uint16_t)(amount + step)) {
		if (amount > 16 * ANGLE_ONE)
			break;
		a = (int16_t)(left ? k->angle - amount : k->angle + amount);
		if (a < 0)
			a = (int16_t)(a + ANGLE_MAX);
		if (a > ANGLE_MAX)
			a = (int16_t)(a - ANGLE_MAX);
		nx = sight_cell(k, (uint16_t)a, &ny);
		if (nx != cx || ny != cy)
			return amount;
	}
	return step;
}

// tankTurn. Holding both keys turns neither way.
static void turn(Tank *k, uint8_t rate, uint8_t keys)
{
	uint8_t left = (keys & (IN_LEFT | IN_RIGHT)) == IN_LEFT;
	uint8_t right = (keys & (IN_LEFT | IN_RIGHT)) == IN_RIGHT;
	uint16_t amount;
	int16_t a;

	if (left) {
		amount = rate;
		if (k->firstLeft < R_TANK_TURN_RAMP) {
			k->firstLeft++;
			amount >>= 3;
			if (k->firstLeft == 1)
				amount = visible_turn(k, amount, 1);
		}
		a = (int16_t)(k->angle - amount);
		if (a < 0)
			a = (int16_t)(a + ANGLE_MAX);
		k->angle = (uint16_t)a;
	} else {
		k->firstLeft = 0;
	}
	if (right) {
		amount = rate;
		if (k->firstRight < R_TANK_TURN_RAMP) {
			k->firstRight++;
			amount >>= 3;
			if (k->firstRight == 1)
				amount = visible_turn(k, amount, 0);
		}
		k->angle = (uint16_t)(k->angle + amount);
		if (k->angle > ANGLE_MAX)
			k->angle = (uint16_t)(k->angle - ANGLE_MAX);
	} else {
		k->firstRight = 0;
	}
}

// tankAccel: cap is the terrain's top speed in quarters.
static void accel(Tank *k, int16_t cap, uint8_t keys)
{
	uint8_t faster = (keys & (IN_FASTER | IN_SLOWER)) == IN_FASTER;
	uint8_t slower = (keys & (IN_FASTER | IN_SLOWER)) == IN_SLOWER;

	if (slower || k->speed > cap) {
		int16_t v = k->speed;

		if (k->speed > cap)
			v = (int16_t)(v - R_TANK_DECEL);
		if (slower)
			v = (int16_t)(v - R_TANK_BRAKE);
		k->speed = v < 0 ? 0 : v;
	} else if (faster && k->speed < cap) {
		k->speed = (int16_t)(k->speed + R_TANK_ACCEL);
		if (k->speed > cap)
			k->speed = cap;
	}
	if (!faster && !slower && (k->opts & OPT_AUTOSLOW) && k->speed > 0)
		k->speed = (int16_t)(k->speed - R_TANK_AUTOSLOW);
}

// tankApplyDamage: more damage than armour kills.
static uint8_t damage(Tank *k, uint8_t amount)
{
	if (amount > k->armour) {
		k->armour = 0;
		k->destroyed = 1;
		return 1;
	}
	k->armour = (uint8_t)(k->armour - amount);
	return 0;
}

static uint8_t big_wreck(const Tank *k)
{
	return (uint8_t)(k->shells + k->mines > R_BIG_EXPLOSION);
}

void tank_mine_damage(GameState *s, uint8_t t, uint8_t mx, uint8_t my)
{
	Tank *k = &s->tanks[t];
	uint16_t cx = (uint16_t)(((uint16_t)mx << 8) + 128U), cy = (uint16_t)(((uint16_t)my << 8) + 128U);
	uint16_t dx = k->x > cx ? k->x - cx : cx - k->x;
	uint16_t dy = k->y > cy ? k->y - cy : cy - k->y;
	uint8_t amount = R_MINE_DAMAGE;

	if (dx >= R_MINE_DAMAGE_RANGE || dy >= R_MINE_DAMAGE_RANGE || k->destroyed)
		return;
	if (amount > k->armour)
		amount = (uint8_t)(amount - amount / R_MINE_FATAL_DIVISOR);
	if (damage(k, amount)) {
		g_tank_explosion(s, k->x, k->y, k->angle, (uint8_t)(k->speed >> 2), big_wreck(k), k->player);
		k->deathWait = R_TANK_DEATH_TICKS;
		pills_drop(s, t);
	}
	if (k->boat) {
		k->boat = BOAT_OFF;
		k->speed = 0;
	}
}

uint8_t tank_shell_hit(GameState *s, uint8_t t, uint16_t wx, uint16_t wy, uint16_t angle)
{
	Tank *k = &s->tanks[t];
	int16_t dx = (int16_t)(k->x - wx), dy = (int16_t)(k->y - wy);

	if (k->destroyed || dx <= -R_TANK_HIT_RADIUS || dx >= R_TANK_HIT_RADIUS ||
	    dy <= -R_TANK_HIT_RADIUS || dy >= R_TANK_HIT_RADIUS ||
	    MUL16(dx, dx) + MUL16(dy, dy) >= (int32_t)R_TANK_HIT_RADIUS * R_TANK_HIT_RADIUS)
		return 0;
	if (k->boat) {
		k->boat = BOAT_OFF;
		k->speed = 0;
	}
	if (damage(k, R_SHELL_DAMAGE)) {
		g_tank_explosion(s, wx, wy, angle, R_TANK_EXPLOSION_LENGTH, big_wreck(k), k->player);
		k->deathWait = R_TANK_DEATH_TICKS;
		pills_drop(s, t);
	} else {
		int16_t bx, by;

		util_distance(&bx, &by, angle, R_SHELL_SPEED);
		k->bumpX = (int32_t)bx * 512;
		k->bumpY = (int32_t)by * 512;
	}
	return 1;
}

// startsIsValidSquare: deep sea without a mine.
static uint8_t start_square_ok(uint8_t x, uint8_t y)
{
	return game_terrain(x, y) == TERRAIN_DEEP_SEA && !g_is_mine(x, y);
}

static uint8_t near(uint8_t ax, uint8_t ay, uint8_t bx, uint8_t by, uint8_t range)
{
	uint8_t dx = ax > bx ? ax - bx : bx - ax, dy = ay > by ? ay - by : by - ay;

	return dx <= range && dy <= range;
}

// Another live tank within range squares; hostile only if asked.
static uint8_t tank_near(const GameState *s, uint8_t self, uint8_t x, uint8_t y,
	uint8_t range, uint8_t hostile)
{
	uint8_t i;

	for (i = 0; i < s->numTanks; i++) {
		const Tank *o = &s->tanks[i];

		if (i == self || o->destroyed)
			continue;
		if (hostile && game_allied(s, o->player, s->tanks[self].player))
			continue;
		if (near((uint8_t)(o->x >> 8), (uint8_t)(o->y >> 8), x, y, range))
			return 1;
	}
	return 0;
}

// A live pill within range: 0 any, 1 not friendly (neutral too), 2 owned
// by another side.
static uint8_t pill_near(const GameState *s, uint8_t player, uint8_t x, uint8_t y, uint8_t kind)
{
	uint8_t i;

	for (i = 0; i < s->map.numPills; i++) {
		const MapPill *p = &s->map.pills[i];

		if (!p->active || p->inTank || p->armour == 0 ||
		    !near(p->x, p->y, x, y, R_START_PILL_RANGE))
			continue;
		if (kind == 0)
			return 1;
		if (game_allied(s, p->owner, player))
			continue;
		if (kind == 1 || p->owner != MAP_NEUTRAL)
			return 1;
	}
	return 0;
}

// startsGetStartOpen: from a random first start, the first ideal one (no
// tank beside it and no pill but friendly ones near), else the first with
// no enemy near, else the first valid, else the first.
static int8_t pick_open(GameState *s, uint8_t t)
{
	uint8_t n = s->map.numStarts, i, player = s->tanks[t].player;
	int8_t friendly = -1, fallback = -1, any = -1;
	uint16_t r = game_random_below(s, n);

	for (i = 0; i < n; i++) {
		uint8_t j = (uint8_t)((r + i) % n);
		const MapStart *st = &s->map.starts[j];

		if (!st->active)
			continue;
		if (any < 0)
			any = (int8_t)j;
		if (!start_square_ok(st->x, st->y))
			continue;
		if (!tank_near(s, t, st->x, st->y, R_START_TANK_RANGE, 0) &&
		    !pill_near(s, player, st->x, st->y, 1))
			return (int8_t)j;
		if (friendly < 0 && !tank_near(s, t, st->x, st->y, R_START_TANK_RANGE, 1) &&
		    !pill_near(s, player, st->x, st->y, 2))
			friendly = (int8_t)j;
		if (fallback < 0)
			fallback = (int8_t)j;
	}
	return friendly >= 0 ? friendly : fallback >= 0 ? fallback : any;
}

enum { TIER_OWN, TIER_NEUTRAL, TIER_SAFE, TIER_FALLBACK, TIER_NONE };

static uint8_t start_tier(const GameState *s, uint8_t t, const MapStart *st)
{
	uint8_t player = s->tanks[t].player, i, own = 0, neutral = 0;

	if (!st->active || !start_square_ok(st->x, st->y))
		return TIER_NONE;
	if (tank_near(s, t, st->x, st->y, R_START_TANK_RANGE, 1) ||
	    pill_near(s, player, st->x, st->y, 2))
		return TIER_FALLBACK;
	for (i = 0; i < s->map.numBases; i++) {
		const MapBase *b = &s->map.bases[i];

		if (!b->active || b->armour <= R_BASE_CAPTURE_ARMOUR ||
		    !near(b->x, b->y, st->x, st->y, R_START_BASE_RANGE))
			continue;
		if (b->owner == MAP_NEUTRAL)
			neutral = 1;
		else if (game_allied(s, b->owner, player))
			own = 1;
		else
			return TIER_FALLBACK;
	}
	if (own)
		return TIER_OWN;
	if (neutral)
		return TIER_NEUTRAL;
	if (!tank_near(s, t, st->x, st->y, R_START_TANK_RANGE, 0) &&
	    !pill_near(s, player, st->x, st->y, 0))
		return TIER_SAFE;
	return TIER_FALLBACK;
}

// startsGetStartTournament: near the player's own bases, or neutral ones
// (taken together while over a fifth of the bases are neutral), else away
// from everything, else anywhere valid; a random start within the tier.
static int8_t pick_tournament(GameState *s, uint8_t t)
{
	uint8_t n = s->map.numStarts, i, tier, live = 0, neutral = 0, want, count;
	uint8_t tiers[MAP_MAX_STARTS];

	for (i = 0; i < s->map.numBases; i++) {
		const MapBase *b = &s->map.bases[i];

		if (!b->active)
			continue;
		live++;
		if (b->owner == MAP_NEUTRAL)
			neutral++;
	}
	for (i = 0; i < n; i++) {
		tiers[i] = start_tier(s, t, &s->map.starts[i]);
		if (live > 0 && (uint16_t)neutral * 100 / live > R_START_NEUTRAL_PCT &&
		    tiers[i] == TIER_NEUTRAL)
			tiers[i] = TIER_OWN;
	}
	for (tier = TIER_OWN; tier < TIER_NONE; tier++) {
		count = 0;
		for (i = 0; i < n; i++)
			count = (uint8_t)(count + (tiers[i] == tier));
		if (count == 0)
			continue;
		want = (uint8_t)game_random_below(s, count);
		for (i = 0; i < n; i++)
			if (tiers[i] == tier && want-- == 0)
				return (int8_t)i;
	}
	for (i = 0; i < n; i++)
		if (s->map.starts[i].active)
			return (int8_t)i;
	return -1;
}

// utilSpiralOffset: step 0 is the centre, then rings clockwise from the
// top left.
static void spiral(uint16_t step, int8_t *dx, int8_t *dy)
{
	uint16_t ring = 1, start = 1, side, off, len;

	if (step == 0) {
		*dx = 0;
		*dy = 0;
		return;
	}
	while (start + 8 * ring <= step) {
		start = (uint16_t)(start + 8 * ring);
		ring++;
	}
	len = (uint16_t)(2 * ring);
	side = (uint16_t)((step - start) / len);
	off = (uint16_t)((step - start) % len);
	switch (side) {
	case 0: *dx = (int8_t)(off - ring); *dy = (int8_t)-ring; break;
	case 1: *dx = (int8_t)ring; *dy = (int8_t)(off - ring); break;
	case 2: *dx = (int8_t)(ring - off); *dy = (int8_t)ring; break;
	default: *dx = (int8_t)-ring; *dy = (int8_t)(ring - off); break;
	}
}

// startsScatterFind: the nearest valid square to the start in a spiral,
// first one clear of other tanks, then any.
static void scatter(const GameState *s, uint8_t t, uint8_t *x, uint8_t *y)
{
	uint8_t pass;
	uint16_t step;

	for (pass = 0; pass < 2; pass++) {
		for (step = 0; step < R_START_SCATTER_MAX; step++) {
			int8_t dx, dy;
			uint8_t nx, ny;

			spiral(step, &dx, &dy);
			nx = (uint8_t)(*x + dx);
			ny = (uint8_t)(*y + dy);
			if (!start_square_ok(nx, ny))
				continue;
			if (pass == 0 && tank_near(s, t, nx, ny, R_START_SPAWN_SEPARATION - 1, 0))
				continue;
			*x = nx;
			*y = ny;
			return;
		}
	}
}

// gameTypeGetItems: what a tank starts with.
static void loadout(const GameState *s, Tank *k)
{
	k->armour = R_TANK_FULL_ARMOUR;
	k->shells = 0;
	k->mines = 0;
	k->trees = 0;
	if (s->gameType == GAME_OPEN) {
		k->shells = R_TANK_FULL_SHELLS;
		k->mines = R_TANK_FULL_MINES;
		k->trees = R_TANK_FULL_TREES;
	} else if (s->gameType == GAME_TOURNAMENT) {
		uint8_t i, live = 0, neutral = 0;

		for (i = 0; i < s->map.numBases; i++) {
			if (!s->map.bases[i].active)
				continue;
			live++;
			neutral = (uint8_t)(neutral + (s->map.bases[i].owner == MAP_NEUTRAL));
		}
		k->shells = (uint8_t)((uint16_t)32 * neutral / (live ? live : 1));
	}
}

// The respawn half of tankDeath: stock by game type, a start by game type,
// scattered off any tank already there, on a boat.
static void respawn(GameState *s, uint8_t t)
{
	Tank *k = &s->tanks[t];
	int8_t i = s->gameType == GAME_OPEN ? pick_open(s, t) : pick_tournament(s, t);

	loadout(s, k);
	k->destroyed = 0;
	k->boat = BOAT_IN;
	k->reload = 0;
	k->speed = 0;
	k->bumpX = 0;
	k->bumpY = 0;
	k->residual = 0;
	k->waterCount = 0;
	k->deathWait = 0;
	if (i < 0) {
		// No start: the middle of the map, on a boat.
		k->x = 0x8080U;
		k->y = 0x8080U;
		k->angle = 0;
	} else {
		const MapStart *st = &s->map.starts[i];
		uint8_t x = st->x, y = st->y;

		scatter(s, t, &x, &y);
		k->x = (uint16_t)(((uint16_t)x << 8) + 128U);
		k->y = (uint16_t)(((uint16_t)y << 8) + 128U);
		k->angle = (uint16_t)(((4 - st->dir) & 15) * 16 * ANGLE_ONE);
	}
}

// tankCreate.
void tank_create(GameState *s, uint8_t t, uint8_t player)
{
	Tank *k = &s->tanks[t];
	uint8_t *p = (uint8_t *)k;
	uint16_t i;

	for (i = 0; i < sizeof(*k); i++)
		p[i] = 0;
	k->player = player;
	k->sightLen = R_GUNSIGHT_MAX;
	k->showSight = 1;
	respawn(s, t);
}

// tankNudgeBuildings: up to five passes push the tank's circle out of the
// solid squares around it, each along the line from the nearest point of
// the square. WinBolo does this in doubles; here the distance is in 1/256
// world units from an integer square root and the push is truncated the
// same way.
static uint8_t nudge(GameState *s, Tank *k)
{
	uint8_t pass, hit = 0;
	int32_t x = k->x, y = k->y;

	for (pass = 0; pass < R_TANK_NUDGE_PASSES; pass++) {
		uint8_t cx = (uint8_t)(x >> 8), cy = (uint8_t)(y >> 8), pushed = 0;
		int8_t ox, oy;

		for (oy = -1; oy <= 1; oy++) {
			for (ox = -1; ox <= 1; ox++) {
				uint8_t sx = (uint8_t)(cx + ox), sy = (uint8_t)(cy + oy);
				int32_t minX = (int32_t)sx << 8, minY = (int32_t)sy << 8, px, py, dx, dy, d2;

				if (!solid(s, sx, sy, k->player))
					continue;
				px = x < minX ? minX : x > minX + 256 ? minX + 256 : x;
				py = y < minY ? minY : y > minY + 256 ? minY + 256 : y;
				dx = x - px;
				dy = y - py;
				d2 = dx * dx + dy * dy;
				if (d2 >= (int32_t)R_TANK_HIT_RADIUS * R_TANK_HIT_RADIUS)
					continue;
				hit = 1;
				pushed = 1;
				if (d2 == 0) {
					// The middle is inside: out by the nearest edge.
					int32_t l = x - minX, r = minX + 256 - x, u = y - minY, d = minY + 256 - y;

					if (l <= r && l <= u && l <= d)
						x -= l + R_TANK_HIT_RADIUS;
					else if (r <= u && r <= d)
						x += r + R_TANK_HIT_RADIUS;
					else if (u <= d)
						y -= u + R_TANK_HIT_RADIUS;
					else
						y += d + R_TANK_HIT_RADIUS;
				} else {
					int32_t dist = util_isqrt((uint32_t)d2 << 16);   // 1/256 units

					x += dx * ((int32_t)R_TANK_HIT_RADIUS * 256 - dist) / dist;
					y += dy * ((int32_t)R_TANK_HIT_RADIUS * 256 - dist) / dist;
				}
				if (x < 0)
					x = 0;
				if (x > 65535)
					x = 65535;
				if (y < 0)
					y = 0;
				if (y > 65535)
					y = 65535;
			}
		}
		if (!pushed)
			break;
	}
	k->x = (uint16_t)x;
	k->y = (uint16_t)y;
	return hit;
}

// tankCheckGroundClear's test: a live pill, a base of another side or a
// building under the tank.
static uint8_t ground_blocked(const GameState *s, const Tank *k, uint8_t x, uint8_t y)
{
	int8_t b = g_base_at(s, x, y);
	uint8_t t;

	if (g_live_pill_at(s, x, y) >= 0)
		return 1;
	if (b >= 0)
		return s->map.bases[b].owner != MAP_NEUTRAL &&
			!game_allied(s, s->map.bases[b].owner, k->player);
	t = game_terrain(x, y);
	return t == TERRAIN_BUILDING || t == TERRAIN_HALFBUILDING;
}

// Leaving the boat: it stays on the river square left last if that is
// near.
static void leave_boat(Tank *k, uint8_t mx, uint8_t my)
{
	uint8_t dx = k->lastRiverX > mx ? k->lastRiverX - mx : mx - k->lastRiverX;
	uint8_t dy = k->lastRiverY > my ? k->lastRiverY - my : my - k->lastRiverY;

	k->boat = BOAT_OFF;
	if (dx + dy <= 2 && game_terrain(k->lastRiverX, k->lastRiverY) == TERRAIN_RIVER)
		game_set_terrain(k->lastRiverX, k->lastRiverY, TERRAIN_BOAT);
}

static uint8_t land_not_boat(const GameState *s, uint8_t x, uint8_t y)
{
	return g_is_land(s, x, y) && game_terrain(x, y) != TERRAIN_BOAT;
}

// The boat part of tankMoveUnified. bmx, bmy is the square at the start of
// the tick and ox, oy the position.
static void boat_move(GameState *s, uint8_t t, uint8_t bmx, uint8_t bmy, uint16_t ox, uint16_t oy)
{
	Tank *k = &s->tanks[t];
	uint8_t mx = (uint8_t)(k->x >> 8), my = (uint8_t)(k->y >> 8);
	uint8_t terr = game_terrain(mx, my), slow = (k->speed >> 2) < R_TANK_BOAT_EXIT_SPEED;

	if (terr == TERRAIN_RIVER) {
		k->lastRiverX = mx;
		k->lastRiverY = my;
	} else if (terr == TERRAIN_BOAT) {
		// Driving a boat into another sinks the other one.
		game_set_terrain(mx, my, TERRAIN_RIVER);
		g_explosion(s, k->x, k->y);
	}
	if (slow && !g_is_land(s, mx, my)) {
		// Keep a slow boat off the bank.
		uint16_t lo = (uint16_t)(((uint16_t)mx << 8) + R_TANK_BOAT_EXIT_INSET);
		uint16_t hi = (uint16_t)(((uint16_t)mx << 8) + 256 - R_TANK_BOAT_EXIT_INSET);

		if (land_not_boat(s, (uint8_t)(mx - 1), my) && k->x < lo)
			k->x = lo;
		if (land_not_boat(s, (uint8_t)(mx + 1), my) && k->x > hi)
			k->x = hi;
		lo = (uint16_t)(((uint16_t)my << 8) + R_TANK_BOAT_EXIT_INSET);
		hi = (uint16_t)(((uint16_t)my << 8) + 256 - R_TANK_BOAT_EXIT_INSET);
		if (land_not_boat(s, mx, (uint8_t)(my - 1)) && k->y < lo)
			k->y = lo;
		if (land_not_boat(s, mx, (uint8_t)(my + 1)) && k->y > hi)
			k->y = hi;
	}
	if (g_is_land(s, mx, my)) {
		if (terr == TERRAIN_BUILDING || terr == TERRAIN_HALFBUILDING) {
			leave_boat(k, mx, my);
		} else if (!slow) {
			leave_boat(k, mx, my);
			if (g_is_mine(mx, my))
				g_mine_exp(s, mx, my);
		} else if (mx != bmx && !g_is_land(s, bmx, my)) {
			k->x = ox;
		} else if (my != bmy && !g_is_land(s, mx, bmy)) {
			k->y = oy;
		} else {
			k->x = ox;
			k->y = oy;
		}
	}
	mx = (uint8_t)(k->x >> 8);
	my = (uint8_t)(k->y >> 8);
	if (k->boat && g_is_mine(mx, my)) {
		k->boat = BOAT_OFF;
		k->speed = 0;
		g_explosion(s, k->x, k->y);
	}
}

// tankMoveUnified for one tank.
static void drive(GameState *s, uint8_t t, const GameInput *in)
{
	Tank *k = &s->tanks[t];
	uint8_t mx = (uint8_t)(k->x >> 8), my = (uint8_t)(k->y >> 8);
	uint8_t bmx = mx, bmy = my, terr, moved = 0;
	uint16_t ox = k->x, oy = k->y;
	int16_t dx, dy;
	int8_t b;

	// tankCheckGroundClear: a tank on something solid is pushed towards
	// the nearer edge and slowed.
	if (ground_blocked(s, k, mx, my)) {
		k->x = (uint16_t)(k->x + ((k->x & 0xFF) < 128 ? -1 : 1));
		k->y = (uint16_t)(k->y + ((k->y & 0xFF) < 128 ? -1 : 1));
		k->speed = (int16_t)(k->speed - 4);
		if (k->speed < 0)
			k->speed = 0;
	}
	turn(k, turn_at(s, mx, my, k->boat, k->player), in->keys);
	accel(k, (int16_t)(g_speed_at(s, mx, my, k->boat, k->player) * 4), in->keys);

	// Moves by whole world units in one of 16 directions once enough speed
	// has built up.
	k->residual = (uint8_t)(k->residual + (k->speed >> 2));
	if (k->residual >= R_TANK_MIN_MOVE) {
		util_distance(&dx, &dy, (uint16_t)(util_dir16(k->angle) * 16 * ANGLE_ONE), k->residual);
		k->residual = 0;
		k->x = (uint16_t)(k->x + dx);
		k->y = (uint16_t)(k->y + dy);
		moved = 1;
	}
	// Shell knockback.
	if (k->bumpX || k->bumpY) {
		dx = (int16_t)(k->bumpX >= 0 ? k->bumpX >> 9 : -((-k->bumpX) >> 9));
		dy = (int16_t)(k->bumpY >= 0 ? k->bumpY >> 9 : -((-k->bumpY) >> 9));
		k->x = (uint16_t)(k->x + dx);
		k->y = (uint16_t)(k->y + dy);
		moved = 1;
		k->bumpX = k->bumpX >= 0 ? k->bumpX - ((k->bumpX >> R_TANK_BUMP_DECAY_SHIFT) + 1) :
			k->bumpX + (((-k->bumpX) >> R_TANK_BUMP_DECAY_SHIFT) + 1);
		k->bumpY = k->bumpY >= 0 ? k->bumpY - ((k->bumpY >> R_TANK_BUMP_DECAY_SHIFT) + 1) :
			k->bumpY + (((-k->bumpY) >> R_TANK_BUMP_DECAY_SHIFT) + 1);
		if (k->bumpX > -2 && k->bumpX < 2)
			k->bumpX = 0;
		if (k->bumpY > -2 && k->bumpY < 2)
			k->bumpY = 0;
	}
	if (nudge(s, k) && moved && ((k->x ^ ox) & 0xFFF0) == 0 && ((k->y ^ oy) & 0xFFF0) == 0)
		k->obstructed = 1;
	pills_pickup(s, t);

	if (k->boat) {
		boat_move(s, t, bmx, bmy, ox, oy);
		return;
	}
	mx = (uint8_t)(k->x >> 8);
	my = (uint8_t)(k->y >> 8);
	terr = game_terrain(mx, my);
	if (terr == TERRAIN_BOAT) {
		game_set_terrain(mx, my, TERRAIN_RIVER);
		k->boat = BOAT_IN;
		k->lastRiverX = mx;
		k->lastRiverY = my;
		return;
	}
	if ((mx != bmx || my != bmy) && g_is_mine(mx, my))
		g_mine_exp(s, mx, my);
	// Wading: a slow tank in a river loses stock.
	b = g_base_at(s, mx, my);
	if (terr == TERRAIN_RIVER && b < 0 && k->speed <= R_SPEED_RIVER * 4) {
		if (++k->waterCount >= R_TANK_WATER_TICKS) {
			k->waterCount = 0;
			if (k->shells >= R_WATER_LOSS_SHELLS)
				k->shells = (uint8_t)(k->shells - R_WATER_LOSS_SHELLS);
			if (k->mines >= R_WATER_LOSS_MINES)
				k->mines = (uint8_t)(k->mines - R_WATER_LOSS_MINES);
		}
	}
	if (b >= 0)
		bases_capture(s, (uint8_t)b, k->player);
	pills_pickup(s, t);
}

// tankLayMine.
static void lay_mine(GameState *s, Tank *k)
{
	uint8_t mx = (uint8_t)(k->x >> 8), my = (uint8_t)(k->y >> 8);
	uint8_t t = game_terrain(mx, my);

	if (g_is_mine(mx, my) || k->mines == 0 || k->boat || k->destroyed)
		return;
	if (t == TERRAIN_BUILDING || t == TERRAIN_HALFBUILDING || t == TERRAIN_BOAT ||
	    t == TERRAIN_RIVER || t == TERRAIN_DEEP_SEA)
		return;
	if (g_pill_at(s, mx, my) >= 0 || g_base_at(s, mx, my) >= 0)
		return;
	k->mines--;
	game_set_terrain(mx, my, (uint8_t)(t + TERRAIN_MINE_ADD));
}

// tankUpdate, with the gunsight and mine keys the client handles first and
// after.
void tank_update(GameState *s, uint8_t t, const GameInput *in)
{
	Tank *k = &s->tanks[t];

	k->opts = in->opts;
	if ((in->keys & IN_SIGHT_FAR) && k->sightLen < R_GUNSIGHT_MAX) {
		k->sightLen++;
		if (k->sightLen == R_GUNSIGHT_MAX && (k->opts & OPT_AUTOSIGHT))
			k->showSight = 0;
	}
	if ((in->keys & IN_SIGHT_NEAR) && k->sightLen > R_GUNSIGHT_MIN) {
		k->sightLen--;
		if (k->opts & OPT_AUTOSIGHT)
			k->showSight = 1;
	}

	k->obstructed = 0;
	if (k->justFired > 0)
		k->justFired--;
	if (k->reload > 0)
		k->reload--;
	if ((in->keys & IN_FIRE) && k->reload == 0 && k->shells > 0 && !k->destroyed) {
		// Life 1 + shell_life * sightLen / 2 - start_add; WinBolo works out
		// shell_life * len in floats and truncates, 4 * sightLen exactly.
		int16_t life = (int16_t)(1 + (R_SHELL_LIFE / 2) * k->sightLen - R_SHELL_START_ADD);

		shells_add(s, k->x, k->y, k->angle, (uint8_t)(life < 0 ? 0 : life), k->player, k->boat);
		k->reload = R_TANK_RELOAD_TICKS;
		k->shells--;
		k->justFired = R_JUST_FIRED_TICKS;
	}
	if (k->deathWait > 0) {
		if (--k->deathWait == 0) {
			k->deaths++;
			respawn(s, t);
		}
		return;
	}
	if (k->destroyed) {
		k->deaths++;
		respawn(s, t);
		return;
	}
	if (!k->boat && game_terrain((uint8_t)(k->x >> 8), (uint8_t)(k->y >> 8)) == TERRAIN_DEEP_SEA) {
		// Drowned: no wreck, just the wait.
		k->armour = 0;
		k->destroyed = 1;
		k->speed = 0;
		k->deathWait = R_TANK_DEATH_TICKS;
		pills_drop(s, t);
		return;
	}
	drive(s, t, in);
	if (in->keys & IN_MINE)
		lay_mine(s, k);
}
