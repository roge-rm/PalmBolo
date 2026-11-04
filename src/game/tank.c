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

uint8_t g_speed_at(const GameState *s, uint8_t x, uint8_t y, uint8_t onBoat)
{
	int8_t p = g_pill_at(s, x, y);
	uint8_t t;

	if (p >= 0 && s->map.pills[p].armour > 0)
		return R_SPEED_SOLID;
	if (g_base_at(s, x, y) >= 0)
		return R_SPEED_BASE;
	if (onBoat)
		return R_SPEED_BOAT;
	t = game_terrain(x, y);
	return t < 16 ? kSpeed[t] : R_SPEED_DEEP_SEA;
}

static uint8_t turn_at(const GameState *s, uint8_t x, uint8_t y, uint8_t onBoat)
{
	int8_t p = g_pill_at(s, x, y);
	uint8_t t;

	if (p >= 0 && s->map.pills[p].armour > 0)
		return R_TURN_SOLID;
	if (g_base_at(s, x, y) >= 0)
		return R_TURN_BASE;
	if (onBoat)
		return R_TURN_BOAT;
	t = game_terrain(x, y);
	return t < 16 ? kTurn[t] : R_TURN_DEEP_SEA;
}

// Solid for a tank: buildings and live pills.
static uint8_t solid(const GameState *s, uint8_t x, uint8_t y)
{
	int8_t p = g_pill_at(s, x, y);
	uint8_t t;

	if (p >= 0 && s->map.pills[p].armour > 0)
		return 1;
	if (g_base_at(s, x, y) >= 0)
		return 0;
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

static void blow_up(GameState *s, Tank *k)
{
	g_tank_explosion(s, k->x, k->y, k->angle, (uint8_t)(k->speed >> 2),
		(uint8_t)(k->shells + k->mines > R_BIG_EXPLOSION), k->player);
	k->deathWait = R_TANK_DEATH_TICKS;
	if (k->boat) {
		k->boat = BOAT_OFF;
		k->speed = 0;
	}
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
	if (damage(k, amount))
		blow_up(s, k);
}

// A start is ideal with no live pill within 9 squares (all pills are
// hostile with one player); WinBolo's startsGetStartOpen picks from a random
// first start, ideal ones before any other.
static void respawn(GameState *s, Tank *k)
{
	uint8_t n = s->map.numStarts, i, j, best = 255, any = 255;
	uint16_t r = game_random_below(s, n);

	for (i = 0; i < n && best == 255; i++) {
		const MapStart *st = &s->map.starts[(r + i) % n];
		uint8_t ok = 1;

		if (game_terrain(st->x, st->y) != TERRAIN_DEEP_SEA || g_is_mine(st->x, st->y))
			continue;
		if (any == 255)
			any = (uint8_t)((r + i) % n);
		for (j = 0; j < s->map.numPills; j++) {
			const MapPill *p = &s->map.pills[j];
			uint8_t ax = p->x > st->x ? p->x - st->x : st->x - p->x;
			uint8_t ay = p->y > st->y ? p->y - st->y : st->y - p->y;

			if (p->armour > 0 && ax <= R_START_PILL_RANGE && ay <= R_START_PILL_RANGE)
				ok = 0;
		}
		if (ok)
			best = (uint8_t)((r + i) % n);
	}
	if (best == 255)
		best = any;
	if (best == 255) {
		// No start: the middle of the map, on a boat.
		k->x = 0x8080U;
		k->y = 0x8080U;
		k->angle = 0;
	} else {
		const MapStart *st = &s->map.starts[best];

		k->x = (uint16_t)(((uint16_t)st->x << 8) + 128U);
		k->y = (uint16_t)(((uint16_t)st->y << 8) + 128U);
		k->angle = (uint16_t)(((4 - st->dir) & 15) * 16 * ANGLE_ONE);
	}
}

// tankCreate and the respawn half of tankDeath.
void tank_new(GameState *s, uint8_t t, uint8_t player)
{
	Tank *k = &s->tanks[t];
	uint16_t deaths = k->deaths;
	uint8_t opts = k->opts, sight = k->sightLen;

	{
		uint8_t *p = (uint8_t *)k;
		uint16_t i;

		for (i = 0; i < sizeof(*k); i++)
			p[i] = 0;
	}
	k->player = player;
	k->opts = opts;
	k->deaths = deaths;
	k->armour = R_TANK_FULL_ARMOUR;
	k->shells = R_TANK_FULL_SHELLS;
	k->mines = R_TANK_FULL_MINES;
	k->trees = 0;
	k->sightLen = sight ? sight : R_GUNSIGHT_MAX;
	k->showSight = 1;
	k->boat = BOAT_IN;
	respawn(s, k);
	k->lastRiverX = (uint8_t)(k->x >> 8);
	k->lastRiverY = (uint8_t)(k->y >> 8);
}

// Moves the tank, keeping it out of solid squares one axis at a time.
// WinBolo pushes the tank's 112-unit circle out of each solid square; this
// stops the tank at the square edge instead.
static void move(GameState *s, Tank *k, int16_t dx, int16_t dy)
{
	uint16_t nx = (uint16_t)(k->x + dx), ny = (uint16_t)(k->y + dy);
	uint8_t oy = (uint8_t)(k->y >> 8);

	if (!solid(s, (uint8_t)(nx >> 8), oy))
		k->x = nx;
	else
		k->obstructed = 1;
	if (!solid(s, (uint8_t)(k->x >> 8), (uint8_t)(ny >> 8)))
		k->y = ny;
	else
		k->obstructed = 1;
}

// The tank's square changed: boats, mines and leaving the water.
static void new_square(GameState *s, Tank *k, uint8_t ox, uint8_t oy)
{
	uint8_t mx = (uint8_t)(k->x >> 8), my = (uint8_t)(k->y >> 8);
	uint8_t t = game_terrain(mx, my);

	if (k->boat) {
		if (t == TERRAIN_RIVER || t == TERRAIN_DEEP_SEA) {
			k->lastRiverX = mx;
			k->lastRiverY = my;
			return;
		}
		if (t == TERRAIN_BOAT) {
			// Driving a boat into another sinks the other one.
			game_set_terrain(mx, my, TERRAIN_RIVER);
			g_explosion(s, k->x, k->y);
			return;
		}
		if (g_base_at(s, mx, my) >= 0 && game_terrain(ox, oy) == TERRAIN_DEEP_SEA)
			return;
		// Onto land: the boat stays on the water square left behind.
		k->boat = BOAT_OFF;
		if (game_terrain(ox, oy) == TERRAIN_RIVER)
			game_set_terrain(ox, oy, TERRAIN_BOAT);
		if (g_is_mine(mx, my))
			g_mine_exp(s, mx, my);
		return;
	}
	if (t == TERRAIN_BOAT) {
		game_set_terrain(mx, my, TERRAIN_RIVER);
		k->boat = BOAT_IN;
		k->lastRiverX = mx;
		k->lastRiverY = my;
		return;
	}
	if (g_is_mine(mx, my))
		g_mine_exp(s, mx, my);
}

// tankMoveUnified, in the parts one player needs.
static void drive(GameState *s, Tank *k, const GameInput *in)
{
	uint8_t mx = (uint8_t)(k->x >> 8), my = (uint8_t)(k->y >> 8);
	uint8_t ox = mx, oy = my, t;
	int16_t dx, dy;
	int8_t b;

	// tankCheckGroundClear: a tank on a building or live pill is pushed off
	// and slowed.
	if (solid(s, mx, my)) {
		k->obstructed = 1;
		k->x = (uint16_t)(k->x + ((k->x & 0xFF) < 128 ? -1 : 1));
		k->y = (uint16_t)(k->y + ((k->y & 0xFF) < 128 ? -1 : 1));
		k->speed = (int16_t)(k->speed - 4);
		if (k->speed < 0)
			k->speed = 0;
	}
	turn(k, turn_at(s, mx, my, k->boat), in->keys);
	accel(k, (int16_t)(g_speed_at(s, mx, my, k->boat) * 4), in->keys);

	// Moves by whole world units in one of 16 directions once enough speed
	// has built up.
	k->residual = (uint8_t)(k->residual + (k->speed >> 2));
	if (k->residual >= R_TANK_MIN_MOVE) {
		util_distance(&dx, &dy, (uint16_t)(util_dir16(k->angle) * 16 * ANGLE_ONE), k->residual);
		k->residual = 0;
		move(s, k, dx, dy);
	}
	// Shell knockback from other tanks.
	if (k->bumpX || k->bumpY) {
		dx = (int16_t)(k->bumpX >= 0 ? k->bumpX >> 9 : -((-k->bumpX) >> 9));
		dy = (int16_t)(k->bumpY >= 0 ? k->bumpY >> 9 : -((-k->bumpY) >> 9));
		move(s, k, dx, dy);
		k->bumpX = k->bumpX >= 0 ? k->bumpX - ((k->bumpX >> R_TANK_BUMP_DECAY_SHIFT) + 1) :
			k->bumpX + (((-k->bumpX) >> R_TANK_BUMP_DECAY_SHIFT) + 1);
		k->bumpY = k->bumpY >= 0 ? k->bumpY - ((k->bumpY >> R_TANK_BUMP_DECAY_SHIFT) + 1) :
			k->bumpY + (((-k->bumpY) >> R_TANK_BUMP_DECAY_SHIFT) + 1);
		if (k->bumpX > -2 && k->bumpX < 2)
			k->bumpX = 0;
		if (k->bumpY > -2 && k->bumpY < 2)
			k->bumpY = 0;
	}
	mx = (uint8_t)(k->x >> 8);
	my = (uint8_t)(k->y >> 8);
	if (mx != ox || my != oy)
		new_square(s, k, ox, oy);

	// Wading: a slow tank in a river loses stock.
	t = game_terrain(mx, my);
	b = g_base_at(s, mx, my);
	if (!k->boat && t == TERRAIN_RIVER && b < 0 && k->speed <= R_SPEED_RIVER * 4) {
		if (++k->waterCount >= R_TANK_WATER_TICKS) {
			k->waterCount = 0;
			if (k->shells >= R_WATER_LOSS_SHELLS)
				k->shells = (uint8_t)(k->shells - R_WATER_LOSS_SHELLS);
			if (k->mines >= R_WATER_LOSS_MINES)
				k->mines = (uint8_t)(k->mines - R_WATER_LOSS_MINES);
		}
	}
	// A neutral or worn-out base becomes this player's.
	if (b >= 0) {
		MapBase *mb = &s->map.bases[b];

		if (mb->owner != k->player &&
		    (mb->owner == MAP_NEUTRAL || mb->armour <= R_BASE_CAPTURE_ARMOUR))
			mb->owner = k->player;
	}
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
		shells_add(s, k->x, k->y, k->angle, k->sightLen, k->player, k->boat);
		k->reload = R_TANK_RELOAD_TICKS;
		k->shells--;
		k->justFired = R_JUST_FIRED_TICKS;
	}
	if (k->deathWait > 0) {
		if (--k->deathWait == 0) {
			k->deaths++;
			tank_new(s, t, k->player);
		}
		return;
	}
	if (k->destroyed) {
		k->deaths++;
		tank_new(s, t, k->player);
		return;
	}
	if (!k->boat && game_terrain((uint8_t)(k->x >> 8), (uint8_t)(k->y >> 8)) == TERRAIN_DEEP_SEA) {
		// Drowned: no wreck, just the wait.
		k->armour = 0;
		k->destroyed = 1;
		k->speed = 0;
		k->deathWait = R_TANK_DEATH_TICKS;
		return;
	}
	drive(s, k, in);
	if (in->keys & IN_MINE)
		lay_mine(s, k);
}
