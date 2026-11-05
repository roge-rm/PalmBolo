// The builder (WinBolo's lgm, "little green man"): orders, walking out and
// back, the work at the square, being killed and the replacement flying in.
//
// Ported from WinBolo 2 (src/bolo/lgm.c), Copyright (c) 1998-2026 John
// Morrison, GPLv3. This version Copyright (c) 2026 the PalmBolo authors,
// GPLv3.
#include "gamepriv.h"
#include "mapstore.h"
#include "util.h"

enum { LGM_BRAIN_FREE, LGM_BRAIN_PARTIAL, LGM_BRAIN_TOTAL };
#define LGM_MAX_FRAMES 2

// lgmCreate.
void SEC_BUILDER lgm_create(GameState *s, uint8_t t)
{
	Builder *m = &s->men[t];
	uint8_t *p = (uint8_t *)m;
	uint8_t i;

	for (i = 0; i < sizeof(*m); i++)
		p[i] = 0;
	m->inTank = 1;
	m->nextAction = LGM_IDLE;
	m->action = LGM_IDLE;
	m->state = LGM_STATE_IDLE;
	m->pill = LGM_NO_PILL;
}

static uint8_t SEC_BUILDER player_of(const GameState *s, uint8_t t)
{
	return s->tanks[t].player;
}

// mapGetManSpeed. It reads the raw map, so the mined border is whatever
// the map has there.
static uint8_t SEC_BUILDER man_speed(const GameState *s, uint8_t x, uint8_t y, uint8_t player)
{
	int8_t p = g_pill_at(s, x, y), b;
	uint8_t t;

	if (p >= 0) {
		if (s->map.pills[p].armour > 0)
			return R_MAN_SPEED_SOLID;
	} else if ((b = g_base_at(s, x, y)) >= 0) {
		return bases_cant_drive(s, (uint8_t)b, player) ? 0 : R_MAN_SPEED_BASE;
	}
	t = mapstore_get(x, y);
	if (t >= TERRAIN_MINE_FIRST && t <= TERRAIN_MINE_LAST)
		t = (uint8_t)(t - TERRAIN_MINE_ADD);
	switch (t) {
	case TERRAIN_RIVER: return R_MAN_SPEED_RIVER;
	case TERRAIN_SWAMP: return R_MAN_SPEED_SWAMP;
	case TERRAIN_CRATER: return R_MAN_SPEED_CRATER;
	case TERRAIN_ROAD: return R_MAN_SPEED_ROAD;
	case TERRAIN_FOREST: return R_MAN_SPEED_FOREST;
	case TERRAIN_RUBBLE: return R_MAN_SPEED_RUBBLE;
	case TERRAIN_GRASS: return R_MAN_SPEED_GRASS;
	case TERRAIN_BOAT: return R_MAN_SPEED_BOAT;
	case TERRAIN_BUILDING:
	case TERRAIN_HALFBUILDING: return R_MAN_SPEED_SOLID;
	}
	return R_MAN_SPEED_DEEP_SEA;
}

// basesAmOwner: a base of the player's side at the square.
static uint8_t SEC_BUILDER own_base(const GameState *s, uint8_t player, uint8_t x, uint8_t y)
{
	int8_t b = g_base_at(s, x, y);

	return b >= 0 && game_allied(s, s->map.bases[b].owner, player);
}

static uint16_t SEC_BUILDER udiff(uint16_t a, uint16_t b)
{
	return a > b ? (uint16_t)(a - b) : (uint16_t)(b - a);
}

// lgmCheckTankBoat: the tank is on a boat and he is within dist of it on
// both axes.
static uint8_t SEC_BUILDER near_boat(const Builder *m, const Tank *k, uint16_t dist)
{
	return k->boat && udiff(k->x, m->x) < dist && udiff(k->y, m->y) < dist;
}

// tankGetMX: 0 while the tank is dead.
static uint8_t SEC_BUILDER tank_mx(const Tank *k)
{
	return k->destroyed ? 0 : (uint8_t)(k->x >> 8);
}

static uint8_t SEC_BUILDER tank_my(const Tank *k)
{
	return k->destroyed ? 0 : (uint8_t)(k->y >> 8);
}

// tankGiveTrees: capped at a full load.
static void SEC_BUILDER give_trees(Tank *k, uint8_t n)
{
	uint16_t v = (uint16_t)(k->trees + n);

	k->trees = (uint8_t)(v > R_TANK_FULL_TREES ? R_TANK_FULL_TREES : v);
}

// tankGetLgmTrees with perform set: takes n if the tank has them.
static uint8_t SEC_BUILDER take_trees(Tank *k, uint8_t n)
{
	if (k->trees < n)
		return 0;
	k->trees = (uint8_t)(k->trees - n);
	return 1;
}

// tankPutCarriedPill: back on the front of the list.
static void SEC_BUILDER put_pill(Tank *k, uint8_t pill)
{
	uint8_t j;

	if (k->numCarry >= MAP_MAX_PILLS)
		return;
	for (j = k->numCarry; j > 0; j--)
		k->carry[j] = k->carry[j - 1];
	k->carry[0] = pill;
	k->numCarry++;
}

// lgmCheckNewRequest with perform set: whether the order can go ahead,
// paying for it from the tank. A road or wall on forest, or a pill on
// forest, becomes gathering trees, and a wall on a river a boat.
static uint8_t SEC_BUILDER check_request(GameState *s, uint8_t t, uint8_t mx, uint8_t my,
	uint8_t *action, uint8_t *pill, uint8_t *trees, uint8_t *mines)
{
	Builder *m = &s->men[t];
	Tank *k = &s->tanks[t];
	uint8_t tx = tank_mx(k), ty = tank_my(k), tankTrees = k->trees, ok = 1;
	uint8_t pos = game_terrain(mx, my);
	uint8_t isPill = g_pill_at(s, mx, my) >= 0, isBase = g_base_at(s, mx, my) >= 0;
	uint8_t onTank = tx == mx && ty == my;

	*trees = 0;
	*mines = 0;
	*pill = LGM_NO_PILL;
	if (pos == TERRAIN_MINE_FOREST)
		pos = TERRAIN_FOREST;
	switch (*action) {
	case LGM_TREE:
		if (pos != TERRAIN_FOREST || isBase || isPill)
			ok = 0;
		break;
	case LGM_ROAD:
		if (pos == TERRAIN_FOREST && !isPill && !isBase)
			*action = LGM_TREE;
		else if (pos == TERRAIN_BOAT || pos == TERRAIN_DEEP_SEA || pos == TERRAIN_BUILDING ||
		    pos == TERRAIN_HALFBUILDING || isPill || isBase)
			ok = 0;
		else if (pos == TERRAIN_ROAD)
			ok = 0;
		else if (pos == TERRAIN_RIVER && onTank && k->boat)
			ok = 0;
		else if (!take_trees(k, R_LGM_COST_ROAD))
			ok = 0;
		else
			*trees = R_LGM_COST_ROAD;
		break;
	case LGM_BUILDING:
		if (pos == TERRAIN_FOREST && !isPill && !isBase) {
			*action = LGM_TREE;
		} else if (pos == TERRAIN_BOAT || pos == TERRAIN_DEEP_SEA || isPill || isBase) {
			ok = 0;
		} else if (pos == TERRAIN_RIVER && onTank && k->boat) {
			ok = 0;
		} else if (pos == TERRAIN_RIVER) {
			*action = LGM_BOAT;
			if (!take_trees(k, R_LGM_COST_BOAT))
				ok = 0;
			else
				*trees = R_LGM_COST_BOAT;
		} else if (onTank) {
			ok = 0;
		} else if (pos == TERRAIN_HALFBUILDING) {
			if (!take_trees(k, R_LGM_COST_REPAIR_BUILDING))
				ok = 0;
			else
				*trees = R_LGM_COST_REPAIR_BUILDING;
		} else if (pos == TERRAIN_BUILDING) {
			ok = 0;
		} else if (!take_trees(k, R_LGM_COST_BUILDING)) {
			ok = 0;
		} else {
			*trees = R_LGM_COST_BUILDING;
		}
		break;
	case LGM_PILL:
		if (pos == TERRAIN_BOAT || pos == TERRAIN_DEEP_SEA || pos == TERRAIN_BUILDING ||
		    pos == TERRAIN_HALFBUILDING || pos == TERRAIN_RIVER || isBase) {
			ok = 0;
		} else if (pos == TERRAIN_FOREST && !isPill) {
			*action = LGM_TREE;
		} else if (onTank) {
			ok = 0;
		} else if (isPill) {
			// A repair takes a full load, or what the tank has, whatever
			// the damage now: the pill can be shot more on the way.
			if (s->map.pills[g_pill_at(s, mx, my)].armour == R_PILL_MAX_ARMOUR) {
				ok = 0;
			} else if (tankTrees < R_LGM_COST_PILL_REPAIR) {
				ok = 0;
			} else {
				*trees = R_LGM_COST_PILL_REPAIR * R_LGM_PILL_REPAIR_LOAD;
				if (tankTrees < *trees)
					*trees = tankTrees;
				take_trees(k, *trees);
			}
			*pill = LGM_NO_PILL;
		} else if (k->numCarry == 0) {
			ok = 0;
		} else {
			// tankGetCarriedPill: the newest.
			uint8_t j;

			*pill = k->carry[0];
			for (j = 0; j + 1 < k->numCarry; j++)
				k->carry[j] = k->carry[j + 1];
			k->numCarry--;
			if (!take_trees(k, R_LGM_COST_PILL_NEW))
				ok = 0;
			else
				*trees = R_LGM_COST_PILL_NEW;
		}
		break;
	case LGM_BOAT:
		if (pos != TERRAIN_RIVER || isPill || isBase)
			ok = 0;
		else if (!take_trees(k, R_LGM_COST_BOAT))
			ok = 0;
		else
			*trees = R_LGM_COST_BOAT;
		break;
	default:
		// LGM_MINE
		if (pos == TERRAIN_DEEP_SEA || pos == TERRAIN_RIVER || pos == TERRAIN_BUILDING ||
		    pos == TERRAIN_BOAT || pos == TERRAIN_HALFBUILDING || isPill || isBase) {
			ok = 0;
		} else if (k->mines < R_LGM_COST_MINE) {
			ok = 0;
		} else {
			k->mines = (uint8_t)(k->mines - R_LGM_COST_MINE);
			*mines = R_LGM_COST_MINE;
		}
		break;
	}
	// No building on a mine he knows of, and a queued mine there is
	// dropped. What was taken is given back by the caller.
	if (ok && g_mine_known(s, mx, my)) {
		ok = 0;
		if (m->nextAction == LGM_MINE && m->nextX == mx && m->nextY == my)
			m->nextAction = LGM_IDLE;
	}
	return ok;
}

// isqrt of dx * dx + dy * dy, which can pass 32 bits.
static uint32_t SEC_BUILDER dist_of(int32_t dx, int32_t dy)
{
	uint32_t ax = (uint32_t)(dx < 0 ? -dx : dx), ay = (uint32_t)(dy < 0 ? -dy : dy);
	uint32_t hi, lo, r = 0, bit;

	// 64-bit d2 as hi:lo, then the root one bit at a time from 2^16.
	{
		uint32_t a = ax * ax, b = ay * ay;

		lo = a + b;
		hi = (uint32_t)(lo < a);
	}
	for (bit = 1UL << 16; bit != 0; bit >>= 1) {
		uint32_t tr = r | bit, sl, sh;

		// tr * tr as sh:sl; tr < 2^17.
		if (tr >= 65536UL) {
			uint32_t low = tr & 0xFFFF;

			// (65536 + low)^2 = 2^32 + 2 * low * 65536 + low^2
			sh = 1 + (low >> 15);
			sl = (low << 17) + low * low;
			if (sl < (low << 17))
				sh++;
		} else {
			sh = 0;
			sl = tr * tr;
		}
		if (sh < hi || (sh == hi && sl <= lo))
			r = tr;
	}
	return r;
}

// lgmApplyStartOffset: he steps out of the tank 16 units towards his goal,
// if he could walk there (always, near a tank on a boat).
static void SEC_BUILDER start_offset(GameState *s, uint8_t t)
{
	Builder *m = &s->men[t];
	int32_t dx = (int32_t)m->destX - m->x, dy = (int32_t)m->destY - m->y;
	uint32_t dist = dist_of(dx, dy);
	uint8_t player = player_of(s, t);

	if (dist > R_LGM_START_OFFSET) {
		uint16_t nx = (uint16_t)(m->x + dx * R_LGM_START_OFFSET / (int32_t)dist);
		uint16_t ny = (uint16_t)(m->y + dy * R_LGM_START_OFFSET / (int32_t)dist);
		uint8_t bmx = (uint8_t)(m->x >> 8), bmy = (uint8_t)(m->y >> 8);
		uint8_t nbx = (uint8_t)(nx >> 8), nby = (uint8_t)(ny >> 8), ok = 1;

		if (!near_boat(m, &s->tanks[t], R_LGM_BOAT_LEAVE)) {
			if (man_speed(s, bmx, bmy, player) == 0)
				ok = 0;
			if (nby != bmy && man_speed(s, bmx, nby, player) == 0)
				ok = 0;
			if (nbx != bmx && man_speed(s, nbx, nby, player) == 0)
				ok = 0;
		}
		if (ok) {
			m->x = nx;
			m->y = ny;
		}
	}
}

// lgmNewPrimaryRequest.
static void SEC_BUILDER new_request(GameState *s, uint8_t t, uint8_t mx, uint8_t my, uint8_t action)
{
	Builder *m = &s->men[t];
	Tank *k = &s->tanks[t];
	uint8_t pill, trees, mines;

	if (check_request(s, t, mx, my, &action, &pill, &trees, &mines)) {
		m->numTrees = trees;
		m->numMines = mines;
		m->destX = (uint16_t)(((uint16_t)mx << 8) + 128U);
		m->destY = (uint16_t)(((uint16_t)my << 8) + 128U);
		m->x = k->x;
		m->y = k->y;
		start_offset(s, t);
		m->state = LGM_STATE_GOING;
		m->action = action;
		m->inTank = 0;
		// The goal square is blessed: he may step onto it even where he
		// can't walk.
		switch (action) {
		case LGM_ROAD:
			if (game_terrain(mx, my) == TERRAIN_RIVER) {
				m->blessX = mx;
				m->blessY = my;
			}
			break;
		case LGM_PILL:
			m->pill = pill;
			m->blessX = mx;
			m->blessY = my;
			break;
		case LGM_BOAT:
		case LGM_BUILDING:
			m->blessX = mx;
			m->blessY = my;
			break;
		}
	} else {
		m->state = LGM_STATE_IDLE;
		give_trees(k, trees);
		k->mines = (uint8_t)(k->mines + mines > R_TANK_FULL_MINES ? R_TANK_FULL_MINES : k->mines + mines);
		if (pill != LGM_NO_PILL)
			put_pill(k, pill);
	}
}

void SEC_BUILDER lgm_request(GameState *s, uint8_t t, uint8_t x, uint8_t y, uint8_t action)
{
	Builder *m = &s->men[t];

	if (m->isDead && !s->tanks[t].destroyed)
		return;         // still flying in
	if (m->action != LGM_IDLE) {
		// Busy: kept for when he is back, replacing any order kept before.
		m->nextX = x;
		m->nextY = y;
		m->nextAction = action;
		return;
	}
	new_request(s, t, x, y, action);
}

// lgmCheckBlessedSquare: whether the blessed square can still take the work.
static uint8_t SEC_BUILDER blessed_ok(const GameState *s, uint8_t x, uint8_t y, uint8_t action)
{
	uint8_t pos = game_terrain(x, y), isPill = g_pill_at(s, x, y) >= 0;

	switch (action) {
	case LGM_TREE:
		return !(pos == TERRAIN_BUILDING || pos == TERRAIN_HALFBUILDING || pos == TERRAIN_RIVER ||
			pos == TERRAIN_BOAT || pos == TERRAIN_DEEP_SEA || isPill);
	case LGM_ROAD:
		return !(pos == TERRAIN_BUILDING || pos == TERRAIN_HALFBUILDING ||
			pos == TERRAIN_BOAT || pos == TERRAIN_DEEP_SEA || isPill);
	case LGM_BUILDING:
		return !(pos == TERRAIN_BUILDING || pos == TERRAIN_RIVER || pos == TERRAIN_BOAT ||
			pos == TERRAIN_DEEP_SEA || isPill);
	case LGM_PILL:
		return !(pos == TERRAIN_BUILDING || pos == TERRAIN_HALFBUILDING || pos == TERRAIN_RIVER ||
			pos == TERRAIN_BOAT || pos == TERRAIN_DEEP_SEA);
	case LGM_BOAT:
		return pos == TERRAIN_RIVER && !isPill;
	}
	return !(pos == TERRAIN_DEEP_SEA || pos == TERRAIN_RIVER || pos == TERRAIN_BUILDING ||
		pos == TERRAIN_BOAT || pos == TERRAIN_HALFBUILDING || isPill || g_base_at(s, x, y) >= 0);
}

// lgmCheckRemove: a square built over forgets its damage.
static void SEC_BUILDER forget_damage(GameState *s, uint8_t terrain, uint8_t x, uint8_t y)
{
	switch (terrain) {
	case TERRAIN_HALFBUILDING: g_damage_remove(s, DMG_BUILDING, x, y); break;
	case TERRAIN_GRASS: g_damage_remove(s, DMG_GRASS, x, y); break;
	case TERRAIN_RUBBLE: g_damage_remove(s, DMG_RUBBLE, x, y); break;
	case TERRAIN_SWAMP: g_damage_remove(s, DMG_SWAMP, x, y); break;
	}
}

static int8_t SEC_BUILDER pill_index_at(const GameState *s, uint8_t x, uint8_t y)
{
	return g_pill_at(s, x, y);
}

// lgmDoWork: at the square. What he carries back is left in numTrees,
// numMines and pill.
static void SEC_BUILDER do_work(GameState *s, uint8_t t)
{
	Builder *m = &s->men[t];
	uint8_t bmx = (uint8_t)(m->destX >> 8), bmy = (uint8_t)(m->destY >> 8);
	int8_t pi = pill_index_at(s, bmx, bmy);
	uint8_t isPill = pi >= 0, isBase = g_base_at(s, bmx, bmy) >= 0, isMine = 0;
	uint8_t terrain = game_terrain(bmx, bmy);

	if (terrain >= TERRAIN_MINE_FIRST && terrain <= TERRAIN_MINE_LAST) {
		terrain = (uint8_t)(terrain - TERRAIN_MINE_ADD);
		isMine = 1;
	}
	switch (m->action) {
	case LGM_TREE:
		g_mine_exp(s, bmx, bmy);
		if (terrain == TERRAIN_FOREST && !isBase && !isPill) {
			game_set_terrain(bmx, bmy, isMine ? TERRAIN_MINE_GRASS : TERRAIN_GRASS);
			m->numTrees = R_LGM_GATHER_TREES;
			g_sound(SND_FARM_TREE, bmx, bmy, GAME_NO_PLAYER);
		}
		break;
	case LGM_ROAD:
		if (terrain != TERRAIN_BUILDING && terrain != TERRAIN_HALFBUILDING &&
		    terrain != TERRAIN_BOAT && terrain != TERRAIN_DEEP_SEA && !isPill && !isBase) {
			game_set_terrain(bmx, bmy, isMine ? TERRAIN_MINE_ROAD : TERRAIN_ROAD);
			m->numTrees = 0;
			forget_damage(s, terrain, bmx, bmy);
			g_sound(SND_MAN_BUILDING, bmx, bmy, GAME_NO_PLAYER);
		}
		break;
	case LGM_BUILDING:
		if (terrain != TERRAIN_BUILDING && terrain != TERRAIN_RIVER && terrain != TERRAIN_BOAT &&
		    terrain != TERRAIN_DEEP_SEA && !isPill && !isBase) {
			// On a mine he sets it off instead.
			if (!isMine)
				game_set_terrain(bmx, bmy, TERRAIN_BUILDING);
			else
				g_mine_exp(s, bmx, bmy);
			m->numTrees = 0;
			forget_damage(s, terrain, bmx, bmy);
			g_sound(SND_MAN_BUILDING, bmx, bmy, GAME_NO_PLAYER);
		}
		break;
	case LGM_BOAT:
		if (terrain == TERRAIN_RIVER) {
			game_set_terrain(bmx, bmy, TERRAIN_BOAT);
			m->numTrees = 0;
		}
		break;
	case LGM_MINE:
		if (!isPill && !isBase && (terrain == TERRAIN_SWAMP || terrain == TERRAIN_CRATER ||
		    terrain == TERRAIN_ROAD || terrain == TERRAIN_FOREST || terrain == TERRAIN_RUBBLE ||
		    terrain == TERRAIN_GRASS)) {
			if (isMine) {
				g_mine_exp(s, bmx, bmy);
			} else {
				game_set_terrain(bmx, bmy, (uint8_t)(terrain + TERRAIN_MINE_ADD));
				g_mine_laid(s, bmx, bmy, player_of(s, t), 1);
				m->numMines = 0;
				g_sound(SND_LAY_MINE, bmx, bmy, GAME_NO_PLAYER);
			}
		}
		break;
	case LGM_PILL:
		if (m->pill == LGM_NO_PILL) {
			// pillsRepairPos: 4 armour a tree up to full; the trees not
			// needed go back.
			if (isPill) {
				MapPill *p = &s->map.pills[pi];
				uint8_t needed = 0, used;

				if (p->armour < R_PILL_MAX_ARMOUR)
					needed = (uint8_t)((R_PILL_MAX_ARMOUR - p->armour + R_PILL_REPAIR_AMOUNT - 1) /
						R_PILL_REPAIR_AMOUNT);
				used = m->numTrees < needed ? m->numTrees : needed;
				if (used > 0) {
					uint16_t a = (uint16_t)(p->armour + used * R_PILL_REPAIR_AMOUNT);

					p->armour = (uint8_t)(a > R_PILL_MAX_ARMOUR ? R_PILL_MAX_ARMOUR : a);
				}
				m->numTrees = (uint8_t)(m->numTrees - used);
				g_sound(SND_MAN_BUILDING, bmx, bmy, GAME_NO_PLAYER);
			}
		} else if (!isPill && !isBase && !g_mine_known(s, bmx, bmy) &&
		    terrain != TERRAIN_BUILDING && terrain != TERRAIN_HALFBUILDING &&
		    terrain != TERRAIN_RIVER && terrain != TERRAIN_BOAT && terrain != TERRAIN_DEEP_SEA) {
			if (isMine) {
				// A hidden mine under the new pill: it goes off, the square
				// floods and he is caught in it.
				uint16_t c = 128;

				g_mine_exp(s, bmx, bmy);
				g_flood(s, bmx, bmy);
				lgm_blast(s, (uint16_t)(((uint16_t)bmx << 8) + c), (uint16_t)(((uint16_t)bmy << 8) + c));
				g_sound(SND_MINE_EXPLOSION, bmx, bmy, GAME_NO_PLAYER);
				game_set_terrain(bmx, bmy, TERRAIN_CRATER);
			} else {
				// pillsSetPill: full armour, its old speed kept within the
				// limits, and a cool down if that isn't the normal speed.
				MapPill *p = &s->map.pills[m->pill];

				m->numTrees = 0;
				p->x = bmx;
				p->y = bmy;
				g_pill_moved(bmx, bmy);
				p->owner = player_of(s, t);
				p->armour = R_PILL_MAX_ARMOUR;
				if (p->speed < R_PILL_ATTACK_MIN)
					p->speed = R_PILL_ATTACK_MIN;
				else if (p->speed > R_PILL_ATTACK_NORMAL)
					p->speed = R_PILL_ATTACK_NORMAL;
				if (p->speed != R_PILL_ATTACK_NORMAL)
					p->coolDown = R_PILL_COOLDOWN;
				p->inTank = 0;
				p->justSeen = 0;
				m->pill = LGM_NO_PILL;
				g_sound(SND_MAN_BUILDING, bmx, bmy, GAME_NO_PLAYER);
			}
		}
		forget_damage(s, terrain, bmx, bmy);
		break;
	}
}

// lgmMoveAway: one step towards the square, along each axis separately so
// he slides along what blocks him. Fully blocked, he turns back.
static void SEC_BUILDER move_away(GameState *s, uint8_t t)
{
	Builder *m = &s->men[t];
	const Tank *k = &s->tanks[t];
	uint8_t player = player_of(s, t), onBoat = near_boat(m, k, R_LGM_BOAT_LEAVE);
	uint8_t bmx = (uint8_t)(m->x >> 8), bmy = (uint8_t)(m->y >> 8), nbx, nby, speed, noGo = 0;
	uint16_t angle = util_angle(m->x, m->y, m->destX, m->destY);
	int16_t xAdd, yAdd;

	if (onBoat) {
		speed = R_MAN_SPEED_BASE;
	} else {
		// The goal square may read 0 (a river under a road, a wall to
		// repair, a pill); there he walks at base speed.
		speed = man_speed(s, bmx, bmy, player);
		if (speed == 0 && bmx == m->blessX && bmy == m->blessY)
			speed = R_MAN_SPEED_BASE;
	}
	util_distance(&xAdd, &yAdd, angle, speed);
	nbx = (uint8_t)((uint16_t)(m->x + xAdd) >> 8);
	nby = (uint8_t)((uint16_t)(m->y + yAdd) >> 8);

	if (man_speed(s, bmx, nby, player) > 0 || onBoat) {
		m->y = (uint16_t)(m->y + yAdd);
	} else if (bmx == m->blessX && nby == m->blessY && blessed_ok(s, bmx, nby, m->action)) {
		m->y = (uint16_t)(m->y + yAdd);
	} else {
		m->obstructed = LGM_BRAIN_PARTIAL;
		noGo = 1;
		nby = bmy;
	}
	if ((man_speed(s, nbx, nby, player) > 0 && xAdd != 0) || onBoat)
		m->x = (uint16_t)(m->x + xAdd);
	else if (nbx == m->blessX && bmy == m->blessY && blessed_ok(s, nbx, nby, m->action))
		m->x = (uint16_t)(m->x + xAdd);
	else if (noGo || yAdd == 0)
		m->state = LGM_STATE_RETURN;

	if (udiff(m->x, m->destX) <= R_LGM_ARRIVE && udiff(m->y, m->destY) <= R_LGM_ARRIVE) {
		m->waitTime = R_LGM_BUILD_TICKS;
		m->state = LGM_STATE_RETURN;
		do_work(s, t);
	}
}

// lgmBackInTank: hands over what he carries and starts any kept order.
static void SEC_BUILDER back_in_tank(GameState *s, uint8_t t)
{
	Builder *m = &s->men[t];
	Tank *k = &s->tanks[t];

	if (m->numTrees > 0) {
		give_trees(k, m->numTrees);
		m->numTrees = 0;
	}
	if (m->pill != LGM_NO_PILL) {
		put_pill(k, m->pill);
		m->pill = LGM_NO_PILL;
	}
	if (m->numMines > 0) {
		// tankAddMines: lost if they don't all fit.
		if (k->mines + m->numMines <= R_TANK_FULL_MINES)
			k->mines = (uint8_t)(k->mines + m->numMines);
		m->numMines = 0;
	}
	if (m->nextAction != LGM_IDLE) {
		uint8_t a = m->nextAction;

		m->nextAction = LGM_IDLE;
		new_request(s, t, m->nextX, m->nextY, a);
	}
}

// The square of a solid thing (wall, pill or a base not his own) at x, y,
// for running off it.
static uint8_t SEC_BUILDER is_wall(uint8_t t)
{
	return t == TERRAIN_BUILDING || t == TERRAIN_HALFBUILDING;
}

// lgmReturn: one step back to the tank, which waits for him while it is
// dead.
static void SEC_BUILDER go_back(GameState *s, uint8_t t)
{
	Builder *m = &s->men[t];
	const Tank *k = &s->tanks[t];
	uint8_t player = player_of(s, t), onBoat = near_boat(m, k, R_LGM_BOAT_RETURN);
	uint8_t bmx = (uint8_t)(m->x >> 8), bmy = (uint8_t)(m->y >> 8), nbx, nby, speed;
	uint16_t angle = util_angle(m->x, m->y, k->x, k->y);
	int16_t xAdd, yAdd;

	if (k->destroyed || k->x == 0 || k->y == 0)
		return;
	if ((bmx == m->blessX && bmy == m->blessY) || onBoat)
		speed = R_MAN_SPEED_BASE;
	else
		speed = man_speed(s, bmx, bmy, player);
	util_distance(&xAdd, &yAdd, angle, speed);
	nbx = (uint8_t)((uint16_t)(m->x + xAdd) >> 8);
	nby = (uint8_t)((uint16_t)(m->y + yAdd) >> 8);

	if (man_speed(s, bmx, nby, player) > 0 || onBoat) {
		m->y = (uint16_t)(m->y + yAdd);
	} else if (bmx == m->blessX && nby == m->blessY) {
		m->y = (uint16_t)(m->y + yAdd);
	} else {
		m->obstructed = LGM_BRAIN_PARTIAL;
		nby = bmy;
	}
	if (man_speed(s, nbx, nby, player) > 0 || onBoat)
		m->x = (uint16_t)(m->x + xAdd);
	else if (nbx == m->blessX && nby == m->blessY)
		m->x = (uint16_t)(m->x + xAdd);
	else
		m->obstructed = LGM_BRAIN_TOTAL;

	// Off the blessed square it is no longer blessed.
	if ((m->x >> 8) != m->blessX || (m->y >> 8) != m->blessY) {
		m->blessX = 0;
		m->blessY = 0;
	}

	if (m->onTop) {
		// Landed on a wall, pill or base: he walks off it at a fixed
		// speed, crossing only onto more of the same.
		uint8_t sameX = 0, sameY = 0, cur, tx, ty;

		util_distance(&xAdd, &yAdd, angle, R_MAN_SPEED_ON_TOP);
		nbx = (uint8_t)((uint16_t)(m->x + xAdd) >> 8);
		nby = (uint8_t)((uint16_t)(m->y + yAdd) >> 8);
		cur = game_terrain(bmx, bmy);
		tx = game_terrain(nbx, bmy);
		ty = game_terrain(bmx, nby);
		if (is_wall(cur)) {
			sameX = is_wall(tx);
			sameY = is_wall(ty);
		} else if (g_pill_at(s, bmx, bmy) >= 0) {
			sameX = g_pill_at(s, nbx, bmy) >= 0;
			sameY = g_pill_at(s, bmx, nby) >= 0;
		} else if (g_base_at(s, bmx, bmy) >= 0) {
			if (g_base_at(s, nbx, bmy) >= 0)
				sameX = !own_base(s, player, nbx, bmy);
			if (g_base_at(s, bmx, nby) >= 0)
				sameY = !own_base(s, player, bmx, nby);
		} else {
			m->onTop = 0;
		}
		if (bmx == nbx || sameX)
			m->x = (uint16_t)(m->x + xAdd);
		if (bmy == nby || sameY)
			m->y = (uint16_t)(m->y + yAdd);
	}

	if (udiff(m->x, k->x) <= R_LGM_RETURN && udiff(m->y, k->y) <= R_LGM_RETURN) {
		m->state = LGM_STATE_IDLE;
		m->inTank = 1;
		m->action = LGM_IDLE;
		m->blessX = 0;
		m->blessY = 0;
		back_in_tank(s, t);
	}
}

// lgmGetMX and lgmGetMY: 0 when idle, and half a square up and left in
// the parachute.
static uint8_t SEC_BUILDER man_mx(const Builder *m)
{
	if (m->state == LGM_STATE_IDLE)
		return 0;
	return (uint8_t)((uint16_t)(m->x - (m->frame == LGM_HELICOPTER_FRAME ? 128 : 1)) >> 8);
}

static uint8_t SEC_BUILDER man_my(const Builder *m)
{
	if (m->state == LGM_STATE_IDLE)
		return 0;
	return (uint8_t)((uint16_t)(m->y - (m->frame == LGM_HELICOPTER_FRAME ? 128 : 1)) >> 8);
}

// lgmParchutingIn: the replacement flies straight at the place the tank was
// when he died, at 3 units a tick, and walks back from there.
static void SEC_BUILDER parachute(Builder *m)
{
	uint16_t angle = util_angle(m->x, m->y, m->destX, m->destY);
	int16_t xAdd, yAdd;

	util_distance(&xAdd, &yAdd, angle, R_LGM_HELICOPTER_SPEED);
	m->x = (uint16_t)(m->x + xAdd);
	m->y = (uint16_t)(m->y + yAdd);
	if (udiff(m->x, m->destX) <= R_LGM_ARRIVE && udiff(m->y, m->destY) <= R_LGM_ARRIVE) {
		m->isDead = 0;
		m->state = LGM_STATE_RETURN;
		m->frame = 0;
	}
}

void SEC_BUILDER lgm_update(GameState *s, uint8_t t)
{
	Builder *m = &s->men[t];

	if (m->isDead) {
		uint8_t mx, my, pos;

		parachute(m);
		// Landing on a wall, pill or a base not his own puts him on top.
		mx = man_mx(m);
		my = man_my(m);
		pos = game_terrain(mx, my);
		if (is_wall(pos) || g_pill_at(s, mx, my) >= 0 || g_base_at(s, mx, my) >= 0)
			m->onTop = !own_base(s, player_of(s, t), mx, my);
		else
			m->onTop = 0;
	} else if (m->state != LGM_STATE_IDLE) {
		if (++m->frame > LGM_MAX_FRAMES)
			m->frame = 0;
		m->obstructed = LGM_BRAIN_FREE;
		if (m->waitTime > 0)
			m->waitTime--;
		else if (m->state == LGM_STATE_GOING)
			move_away(s, t);
		else if (m->state == LGM_STATE_RETURN && s->tanks[t].deathWait == 0)
			go_back(s, t);
		else if (m->state == LGM_STATE_RETURN)
			m->waitTime = 1;
	}
}

// lgmDropCarriedPill: a pill he was taking to the square lands dead near
// where he fell.
static void SEC_BUILDER drop_pill(GameState *s, uint8_t t)
{
	Builder *m = &s->men[t];

	if (m->pill == LGM_NO_PILL)
		return;
	pills_drop_one(s, m->pill, (uint8_t)(m->x >> 8), (uint8_t)(m->y >> 8), player_of(s, t));
	m->pill = LGM_NO_PILL;
}

// lgmKill: he drops what he carries, and a new man starts at a random
// start, flying to where the tank is now (or where he fell, if the tank is
// in the mined border).
static void SEC_BUILDER kill(GameState *s, uint8_t t)
{
	Builder *m = &s->men[t];
	const Tank *k = &s->tanks[t];
	uint8_t i, live = 0, x = 16, y = 16;

	g_sound(SND_MAN_DYING, (uint8_t)(m->x >> 8), (uint8_t)(m->y >> 8), GAME_NO_PLAYER);
	m->isDead = 1;
	m->frame = LGM_HELICOPTER_FRAME;
	m->numTrees = 0;
	m->numMines = 0;
	m->nextAction = LGM_IDLE;
	drop_pill(s, t);
	m->destX = k->x;
	m->destY = k->y;
	if (m->destX <= ((MAP_EDGE_LOW + 1) << 8) || m->destX >= ((uint16_t)(MAP_EDGE_HIGH - 1) << 8) ||
	    m->destY <= ((MAP_EDGE_LOW + 1) << 8) || m->destY >= ((uint16_t)(MAP_EDGE_HIGH - 1) << 8)) {
		m->destX = m->x;
		m->destY = m->y;
	}
	// startsGetRandStart over the live starts.
	for (i = 0; i < s->map.numStarts; i++)
		live = (uint8_t)(live + (s->map.starts[i].active != 0));
	if (live > 0) {
		uint16_t r = game_random_below(s, live);

		for (i = 0; i < s->map.numStarts; i++) {
			if (!s->map.starts[i].active)
				continue;
			if (r == 0)
				break;
			r--;
		}
		x = s->map.starts[i].x;
		y = s->map.starts[i].y;
	}
	m->x = (uint16_t)(((uint16_t)x << 8) + 128U);
	m->y = (uint16_t)(((uint16_t)y << 8) + 128U);
}

// lgmBlastCatches: within half a square on open ground, or on the very
// square of a wall, pill or base that blew up.
static uint8_t SEC_BUILDER caught(const GameState *s, const Builder *m, uint16_t wx, uint16_t wy)
{
	uint8_t mx = (uint8_t)(wx >> 8), my = (uint8_t)(wy >> 8);
	uint8_t pos = game_terrain(mx, my);
	uint16_t dx = udiff(m->x, wx), dy = udiff(m->y, wy);

	if (is_wall(pos) || g_pill_at(s, mx, my) >= 0 || g_base_at(s, mx, my) >= 0)
		return (m->x >> 8) == mx && (m->y >> 8) == my;
	return dx <= R_LGM_BLAST_RANGE && dy <= R_LGM_BLAST_RANGE &&
		(uint32_t)dx * dx + (uint32_t)dy * dy <= (uint32_t)R_LGM_BLAST_RANGE * R_LGM_BLAST_RANGE;
}

void SEC_BUILDER lgm_blast(GameState *s, uint16_t wx, uint16_t wy)
{
	uint8_t t;

	for (t = 0; t < s->numTanks; t++) {
		Builder *m = &s->men[t];

		if (!m->isDead && !m->inTank && caught(s, m, wx, wy))
			kill(s, t);
	}
}

uint8_t SEC_BUILDER lgm_building_at(const GameState *s, uint8_t x, uint8_t y)
{
	uint8_t t;

	for (t = 0; t < s->numTanks; t++) {
		const Builder *m = &s->men[t];

		if (m->isDead || m->state != LGM_STATE_GOING)
			continue;
		if (m->action != LGM_ROAD && m->action != LGM_BUILDING && m->action != LGM_PILL)
			continue;
		if ((m->destX >> 8) == x && (m->destY >> 8) == y)
			return 1;
	}
	return 0;
}
