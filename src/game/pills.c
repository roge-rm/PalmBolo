// Pillboxes and bases: firing, aiming, damage, anger, picking up and
// dropping pills, base restocking, refuelling and capture.
//
// Ported from WinBolo 2 (src/bolo/pillbox.c, bases.c and the server tick),
// Copyright (c) 1998-2026 John Morrison, GPLv3. This version Copyright (c)
// 2026 the PalmBolo authors, GPLv3.
#include "gamepriv.h"
#include "mapstore.h"
#include "util.h"

static uint16_t SEC_PILLS abs_diff(uint16_t a, uint16_t b)
{
	return a > b ? (uint16_t)(a - b) : (uint16_t)(b - a);
}

static uint8_t SEC_PILLS is_forest(const GameState *s, uint8_t x, uint8_t y)
{
	uint8_t t = game_terrain(x, y);

	return (t == TERRAIN_FOREST || t == TERRAIN_MINE_FOREST) &&
		g_pill_at(s, x, y) < 0 && g_base_at(s, x, y) < 0;
}

// utilIsTankInTrees: the tank's square and the three beside it towards the
// quarter of the square it is in are all forest.
static uint8_t SEC_PILLS in_trees(const GameState *s, uint16_t wx, uint16_t wy)
{
	uint8_t mx = (uint8_t)(wx >> 8), my = (uint8_t)(wy >> 8);
	uint8_t nx = (uint8_t)((wx & 0xFF) >= 128 ? mx + 1 : mx - 1);
	uint8_t ny = (uint8_t)((wy & 0xFF) >= 128 ? my + 1 : my - 1);

	return is_forest(s, mx, my) && is_forest(s, nx, my) &&
		is_forest(s, mx, ny) && is_forest(s, nx, ny);
}

// mapGetSpeed and the boat rules for the aim's guess at where the tank goes.
static uint8_t SEC_PILLS aim_can_move(const GameState *s, uint8_t x, uint8_t y, const Tank *k, uint8_t speed)
{
	if (g_speed_at(s, x, y, k->boat, k->player) == 0)
		return 0;
	return !k->boat || !g_is_land(s, x, y) || speed >= R_TANK_BOAT_EXIT_SPEED;
}

// pillsTargetTank and pillsTargetTankMove: walk the tank on at its speed
// and the shell out along the latest guess until the shell is within the
// hit radius, up to 200 steps. WinBolo hands the solver the tank's 16-way
// direction (0..15) where it reads an angle in bradians, so the tank is
// walked almost due north; that is kept as it is.
static uint16_t SEC_PILLS aim(const GameState *s, uint16_t px, uint16_t py, const Tank *k)
{
	uint8_t speed = (uint8_t)(k->speed >> 2), count = 1, found = 0;
	uint16_t tx = k->x, ty = k->y, est, sx, sy, r = 0;
	int16_t ax, ay, hx, hy;

	if (speed == 0)
		return util_angle(px, py, tx, ty);
	util_distance(&ax, &ay, (uint16_t)(util_dir16(k->angle) * ANGLE_ONE), speed);
	est = util_angle(px, py, tx, ty);
	util_distance(&hx, &hy, est, R_SHELL_SPEED);
	sx = (uint16_t)(px + hx);
	sy = (uint16_t)(py + hy);
	while (!found && count < R_PILL_AIM_ITERATIONS) {
		int16_t dx = (int16_t)(tx - sx), dy = (int16_t)(ty - sy);
		uint8_t bmx, bmy, nbx, nby;

		if (dx > -R_TANK_HIT_RADIUS && dx < R_TANK_HIT_RADIUS &&
		    dy > -R_TANK_HIT_RADIUS && dy < R_TANK_HIT_RADIUS &&
		    MUL16(dx, dx) + MUL16(dy, dy) < (int32_t)R_TANK_HIT_RADIUS * R_TANK_HIT_RADIUS) {
			found = 1;
			r = est;
		}
		count++;
		nbx = (uint8_t)((uint16_t)(tx + ax + (ax >= 0 ? R_TANK_SUBTRACT : -R_TANK_SUBTRACT)) >> 8);
		nby = (uint8_t)((uint16_t)(ty + ay + (ay >= 0 ? R_TANK_SUBTRACT : -R_TANK_SUBTRACT)) >> 8);
		bmx = (uint8_t)(tx >> 8);
		bmy = (uint8_t)(ty >> 8);
		if (aim_can_move(s, bmx, nby, k, speed))
			ty = (uint16_t)(ty + ay);
		if (aim_can_move(s, nbx, bmy, k, speed))
			tx = (uint16_t)(tx + ax);
		est = util_angle(px, py, tx, ty);
		util_distance(&hx, &hy, est, R_SHELL_SPEED);
		sx = (uint16_t)(px + MUL16(count + 5, hx));
		sy = (uint16_t)(py + MUL16(count + 5, hy));
		r = est;
	}
	return r;
}

// Being hit or seeing a base hit: fire faster for a while.
static void SEC_PILLS anger(MapPill *p)
{
	p->coolDown = R_PILL_COOLDOWN;
	if (p->speed > R_PILL_ATTACK_MIN) {
		p->speed = (uint8_t)(p->speed / R_PILL_ANGRY_DIVISOR);
		if (p->speed < R_PILL_ATTACK_MIN)
			p->speed = R_PILL_ATTACK_MIN;
	}
}

// pillsUpdate.
void SEC_PILLS pills_update(GameState *s)
{
	uint8_t i, t;

	for (i = 0; i < s->map.numPills; i++) {
		MapPill *p = &s->map.pills[i];
		uint16_t px, py;
		uint32_t best = 0xFFFFFFFFUL;
		int8_t target = -1;

		if (!p->active)
			continue;
		px = (uint16_t)(((uint16_t)p->x << 8) + 128U);
		py = (uint16_t)(((uint16_t)p->y << 8) + 128U);
		if (p->reload < p->speed)
			p->reload++;
		if (p->coolDown > 0 && --p->coolDown == 0) {
			p->speed++;
			if (p->speed < R_PILL_ATTACK_NORMAL)
				p->coolDown = R_PILL_COOLDOWN;
		}
		if (p->armour == 0 || p->reload < p->speed || p->inTank)
			continue;
		for (t = 0; t < s->numTanks; t++) {
			const Tank *k = &s->tanks[t];
			uint16_t dx, dy;
			uint32_t d2;

			if (game_allied(s, p->owner, k->player) || k->destroyed)
				continue;
			dx = abs_diff(k->x, px);
			dy = abs_diff(k->y, py);
			if ((dx >= R_TREE_HIDE_DIST || dy >= R_TREE_HIDE_DIST) && !k->justFired &&
			    in_trees(s, k->x, k->y))
				continue;
			// utilIsItemInRange: distance at most the range.
			if (dx > R_PILL_RANGE || dy > R_PILL_RANGE)
				continue;
			d2 = (uint32_t)dx * dx + (uint32_t)dy * dy;
			if (d2 <= (uint32_t)R_PILL_RANGE * R_PILL_RANGE && d2 < best) {
				best = d2;
				target = (int8_t)t;
			}
		}
		if (target < 0) {
			p->justSeen = 0;
			continue;
		}
		if (p->justSeen)
			shells_add(s, px, py, aim(s, px, py, &s->tanks[target]),
				R_PILL_FIRE_LIFE, GAME_NO_PLAYER, 0);
		p->justSeen = 1;
		p->reload = 0;
	}
}

// pillsDamagePos.
void SEC_PILLS pills_damage(GameState *s, uint8_t i, uint8_t amount)
{
	MapPill *p = &s->map.pills[i];

	p->armour = (uint8_t)(p->armour > amount ? p->armour - amount : 0);
	if (p->armour > 0)
		anger(p);
}

// pillsBaseHit: pills of the base's side near it get angry.
void SEC_PILLS pills_base_hit(GameState *s, uint8_t b)
{
	const MapBase *mb = &s->map.bases[b];
	uint8_t i;

	for (i = 0; i < s->map.numPills; i++) {
		MapPill *p = &s->map.pills[i];
		int16_t dx = (int16_t)(p->x - mb->x), dy = (int16_t)(p->y - mb->y);

		if (p->active && dx >= -R_PILL_BASE_HIT_RANGE && dx <= R_PILL_BASE_HIT_RANGE &&
		    dy >= -R_PILL_BASE_HIT_RANGE && dy <= R_PILL_BASE_HIT_RANGE &&
		    p->owner != MAP_NEUTRAL && game_allied(s, mb->owner, p->owner) && p->armour > 0)
			anger(p);
	}
}

// tankCheckPillCapture: a dead pill under the tank's middle, or 16 world
// units out from it on either axis, goes in the tank.
void SEC_PILLS pills_pickup(GameState *s, uint8_t t)
{
	Tank *k = &s->tanks[t];
	int8_t ox, oy;

	for (oy = -1; oy <= 1; oy++) {
		for (ox = -1; ox <= 1; ox++) {
			uint8_t mx = (uint8_t)((uint16_t)(k->x + ox * R_TANK_PILL_PICKUP_INSET) >> 8);
			uint8_t my = (uint8_t)((uint16_t)(k->y + oy * R_TANK_PILL_PICKUP_INSET) >> 8);
			int8_t i, j;

			if (mx == 0 || my == 0)
				continue;
			i = g_pill_at(s, mx, my);
			if (i < 0 || s->map.pills[i].armour > 0 || k->numCarry >= MAP_MAX_PILLS)
				continue;
			s->map.pills[i].inTank = 1;
			s->map.pills[i].owner = k->player;
			for (j = (int8_t)k->numCarry; j > 0; j--)
				k->carry[j] = k->carry[j - 1];
			k->carry[0] = (uint8_t)i;
			k->numCarry++;
		}
	}
}

// pillsDropCanLand.
static uint8_t SEC_PILLS can_land(const GameState *s, uint8_t x, uint8_t y)
{
	uint8_t t;

	if (x <= MAP_EDGE_LOW || x >= MAP_EDGE_HIGH || y <= MAP_EDGE_LOW || y >= MAP_EDGE_HIGH)
		return 0;
	if (g_pill_at(s, x, y) >= 0 || g_base_at(s, x, y) >= 0)
		return 0;
	t = mapstore_get(x, y);
	return t != TERRAIN_BUILDING && t != TERRAIN_HALFBUILDING && t != TERRAIN_BOAT;
}

// pillsDropSpiral: puts a pill down dead on the nearest free square to
// cx, cy, searched in a square spiral out to the mined border.
uint8_t SEC_PILLS pills_drop_one(GameState *s, uint8_t pill, uint8_t cx, uint8_t cy, uint8_t owner)
{
	int16_t x = cx, y = cy, maxRing, m;
	MapPill *p;

	maxRing = (int16_t)(cx - (MAP_EDGE_LOW + 1));
	m = (int16_t)((MAP_EDGE_HIGH - 1) - cx);
	if (m > maxRing)
		maxRing = m;
	m = (int16_t)(cy - (MAP_EDGE_LOW + 1));
	if (m > maxRing)
		maxRing = m;
	m = (int16_t)((MAP_EDGE_HIGH - 1) - cy);
	if (m > maxRing)
		maxRing = m;
	for (;;) {
		int16_t dx = (int16_t)(x - cx), dy = (int16_t)(y - cy), ring;

		ring = (int16_t)((dx < 0 ? -dx : dx) > (dy < 0 ? -dy : dy) ?
			(dx < 0 ? -dx : dx) : (dy < 0 ? -dy : dy));
		if (ring > maxRing)
			return 0;
		if (can_land(s, (uint8_t)x, (uint8_t)y))
			break;
		if (ring == 0)
			y--;
		else if (dy == -ring && dx < 0) {
			x++;
			if (x == cx)
				y--;
		} else if (dx == -ring)
			y--;
		else if (dy == ring)
			x--;
		else if (dx == ring)
			y++;
		else
			x++;
	}
	p = &s->map.pills[pill];
	p->x = (uint8_t)x;
	p->y = (uint8_t)y;
	p->armour = 0;
	p->owner = owner;
	p->speed = R_PILL_ATTACK_NORMAL;
	p->reload = R_PILL_ATTACK_NORMAL;
	p->coolDown = 0;
	p->justSeen = 0;
	p->inTank = 0;
	return 1;
}

// tankDropPills: a dead tank's pills land dead on the nearest free squares
// to it, newest pill first. Each search starts again from the middle,
// which finds the same squares as carrying on would.
void SEC_PILLS pills_drop(GameState *s, uint8_t t)
{
	Tank *k = &s->tanks[t];
	uint8_t cx = (uint8_t)(k->x >> 8), cy = (uint8_t)(k->y >> 8), j;

	while (k->numCarry > 0) {
		if (!pills_drop_one(s, k->carry[0], cx, cy, k->player))
			return;
		for (j = 0; j + 1 < k->numCarry; j++)
			k->carry[j] = k->carry[j + 1];
		k->numCarry--;
	}
}

// basesCantDrive: a base held by someone else with armour left is solid.
uint8_t SEC_PILLS bases_cant_drive(const GameState *s, uint8_t b, uint8_t player)
{
	const MapBase *mb = &s->map.bases[b];

	return mb->owner != MAP_NEUTRAL && !game_allied(s, mb->owner, player) &&
		mb->armour > R_BASE_CAPTURE_ARMOUR;
}

// basesCanHit: shells from a player hit bases of other sides with armour.
uint8_t SEC_PILLS bases_can_hit(const GameState *s, uint8_t b, uint8_t hitBy)
{
	const MapBase *mb = &s->map.bases[b];

	return hitBy != GAME_NO_PLAYER && mb->owner != MAP_NEUTRAL &&
		!game_allied(s, mb->owner, hitBy) && mb->armour > R_BASE_HIT_ARMOUR;
}

void SEC_PILLS bases_damage(GameState *s, uint8_t b)
{
	MapBase *mb = &s->map.bases[b];

	mb->armour = (uint8_t)(mb->armour > R_SHELL_DAMAGE ? mb->armour - R_SHELL_DAMAGE : 0);
}

// The capture half of tankMoveUnified with basesSetOwner: a neutral or
// worn-out base of another side becomes the player's, and one taken from
// a player is emptied.
void SEC_PILLS bases_capture(GameState *s, uint8_t b, uint8_t player)
{
	MapBase *mb = &s->map.bases[b];

	if (game_allied(s, mb->owner, player))
		return;
	if (mb->owner != MAP_NEUTRAL && mb->armour > R_BASE_CAPTURE_ARMOUR)
		return;
	if (mb->owner != MAP_NEUTRAL) {
		mb->armour = 0;
		mb->shells = 0;
		mb->mines = 0;
	}
	mb->owner = player;
	g_sound(SND_BASE_CAPTURE, mb->x, mb->y, player);
}

static void SEC_PILLS restock(uint8_t *v, uint8_t full)
{
	if (*v < full)
		(*v)++;
}

// basesUpdate and the server's refuel loop. Each player in the game has a
// restock timer; when one runs out every base gains one of each. Then a
// base gives a tank of its side on it armour, then shells, then mines, one
// helping at a time.
void SEC_PILLS bases_update(GameState *s)
{
	uint8_t i, t;

	for (i = 0; i < GAME_MAX_PLAYERS; i++) {
		if (!s->players[i].inUse)
			continue;
		if (--s->baseTimer[i] == 0) {
			uint8_t j;

			for (j = 0; j < s->map.numBases; j++) {
				MapBase *b = &s->map.bases[j];

				if (!b->active)
					continue;
				restock(&b->armour, R_BASE_FULL_ARMOUR);
				restock(&b->shells, R_BASE_FULL_SHELLS);
				restock(&b->mines, R_BASE_FULL_MINES);
			}
			s->baseTimer[i] = R_BASE_REGEN_TICKS;
		}
	}
	for (i = 0; i < s->map.numBases; i++)
		if (s->map.bases[i].active && s->baseTime[i] > 0)
			s->baseTime[i]--;
	for (i = 0; i < s->map.numBases; i++) {
		MapBase *b = &s->map.bases[i];
		uint8_t on = 0;

		if (!b->active)
			continue;
		for (t = 0; t < s->numTanks; t++) {
			Tank *k = &s->tanks[t];

			if (k->destroyed || (k->x >> 8) != b->x || (k->y >> 8) != b->y)
				continue;
			on = 1;
			if (s->baseStopped[i]) {
				s->baseStopped[i] = 0;
				s->baseTime[i] = R_BASE_REFUEL_ARMOUR;
				continue;
			}
			if (s->baseTime[i] != 0 || !game_allied(s, b->owner, k->player))
				continue;
			if (k->armour < R_TANK_FULL_ARMOUR &&
			    b->armour >= R_BASE_MIN_ARMOUR + R_BASE_ARMOUR_GIVE) {
				uint8_t give = R_TANK_FULL_ARMOUR - k->armour;

				if (give > R_BASE_ARMOUR_GIVE)
					give = R_BASE_ARMOUR_GIVE;
				k->armour = (uint8_t)(k->armour + give);
				b->armour = (uint8_t)(b->armour - give);
				s->baseTime[i] = R_BASE_REFUEL_ARMOUR;
			} else if (k->shells < R_TANK_FULL_SHELLS && b->shells > R_BASE_MIN_SHELLS) {
				k->shells++;
				b->shells--;
				s->halfShell ^= 1;
				s->baseTime[i] = (uint8_t)(R_BASE_REFUEL_HALF + s->halfShell);
			} else if (k->mines < R_TANK_FULL_MINES && b->mines > R_BASE_MIN_MINES) {
				k->mines++;
				b->mines--;
				s->halfMine ^= 1;
				s->baseTime[i] = (uint8_t)(R_BASE_REFUEL_HALF + s->halfMine);
			}
		}
		if (!on)
			s->baseStopped[i] = 1;
	}
}
