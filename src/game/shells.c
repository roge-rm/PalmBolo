// Shells: flight, terrain hits and the explosion where they land.
//
// Ported from WinBolo 2 (src/bolo/shells.c), Copyright (c) 1998-2026 John
// Morrison, GPLv3. This version Copyright (c) 2026 the PalmBolo authors,
// GPLv3.
#include "gamepriv.h"
#include "mapstore.h"
#include "util.h"

// shellsAddItem. The shell starts five steps ahead and lives life ticks.
void SEC_SHELLS shells_add(GameState *s, uint16_t x, uint16_t y, uint16_t angle,
	uint8_t life, uint8_t owner, uint8_t onBoat)
{
	Shell *h;
	int16_t dx, dy;
	uint8_t i;

	g_sound(SND_SHOOT, (uint8_t)(x >> 8), (uint8_t)(y >> 8), owner);
	util_distance(&dx, &dy, angle, R_SHELL_SPEED);
	x = (uint16_t)(x + R_SHELL_START_ADD * dx);
	y = (uint16_t)(y + R_SHELL_START_ADD * dy);
	// Newest first; a full list loses its oldest shell.
	i = s->numShells < GAME_MAX_SHELLS ? s->numShells : GAME_MAX_SHELLS - 1;
	for (; i > 0; i--)
		s->shells[i] = s->shells[i - 1];
	if (s->numShells < GAME_MAX_SHELLS)
		s->numShells++;
	h = &s->shells[0];
	h->x = x;
	h->y = y;
	h->angle = angle;
	util_shell_step(&h->stepX, &h->stepY, angle);
	h->accX = 0;
	h->accY = 0;
	h->length = life;
	h->owner = owner;
	h->onBoat = onBoat;
}

// mapIsPassable for a shell.
static uint8_t SEC_SHELLS passable(uint8_t t, uint8_t onBoat)
{
	if (t >= TERRAIN_MINE_FIRST && t <= TERRAIN_MINE_LAST)
		t = (uint8_t)(t - TERRAIN_MINE_ADD);
	switch (t) {
	case TERRAIN_DEEP_SEA:
	case TERRAIN_RIVER:
	case TERRAIN_CRATER:
		return 1;
	case TERRAIN_SWAMP:
	case TERRAIN_ROAD:
	case TERRAIN_RUBBLE:
	case TERRAIN_GRASS:
		return !onBoat;
	}
	return 0;
}

// shellsCheckRoad: a road hit from a side with water beyond becomes river.
static uint8_t SEC_SHELLS check_road(GameState *s, uint8_t x, uint8_t y, uint16_t angle)
{
	uint8_t d = util_dir16(angle);

	if (d < 2 || d >= 14)
		return g_is_land(s, x, (uint8_t)(y - 1)) ? TERRAIN_ROAD : TERRAIN_RIVER;
	if (d < 6)
		return g_is_land(s, (uint8_t)(x + 1), y) ? TERRAIN_ROAD : TERRAIN_RIVER;
	if (d < 10)
		return g_is_land(s, x, (uint8_t)(y + 1)) ? TERRAIN_ROAD : TERRAIN_RIVER;
	return g_is_land(s, (uint8_t)(x - 1), y) ? TERRAIN_ROAD : TERRAIN_RIVER;
}

// shellsCalcCollision: a live pill, then a tank of another player, then a
// base, then terrain the shell can't pass. Returns 0 for no hit, 1 for a
// hit and 2 for a tank hit (which sets off no mine). A base hit moves the
// explosion to the middle of the base.
static uint8_t SEC_SHELLS collide(GameState *s, const Shell *h, uint16_t *pwx, uint16_t *pwy)
{
	uint16_t wx = *pwx, wy = *pwy;
	uint8_t mx = (uint8_t)(wx >> 8), my = (uint8_t)(wy >> 8);
	int8_t p = g_live_pill_at(s, mx, my), b;
	uint8_t t, mine = 0, nt, i;

	// A pill, base or terrain hit explodes in the middle of the square.
	if (p >= 0) {
		*pwx = (uint16_t)(((uint16_t)mx << 8) + 128U);
		*pwy = (uint16_t)(((uint16_t)my << 8) + 128U);
		pills_damage(s, (uint8_t)p, R_PILL_SHELL_DAMAGE);
		g_sound(SND_SHOT_BUILDING, mx, my, GAME_NO_PLAYER);
		return 1;
	}
	for (i = 0; i < s->numTanks; i++) {
		if (s->tanks[i].player != h->owner && tank_shell_hit(s, i, wx, wy, h->angle)) {
			g_sound(SND_HIT_TANK, mx, my, s->tanks[i].player);
			return 2;
		}
	}
	b = g_base_at(s, mx, my);
	if (b >= 0) {
		uint8_t hit = bases_can_hit(s, (uint8_t)b, h->owner);

		if (!h->onBoat && !hit)
			return 0;
		*pwx = (uint16_t)(((uint16_t)mx << 8) + 128U);
		*pwy = (uint16_t)(((uint16_t)my << 8) + 128U);
		if (hit) {
			bases_damage(s, (uint8_t)b);
			pills_base_hit(s, (uint8_t)b);
		}
		g_sound(SND_SHOT_BUILDING, mx, my, GAME_NO_PLAYER);
		return 1;
	}
	if (passable(mapstore_get(mx, my), h->onBoat))
		return 0;
	*pwx = (uint16_t)(((uint16_t)mx << 8) + 128U);
	*pwy = (uint16_t)(((uint16_t)my << 8) + 128U);
	t = game_terrain(mx, my);
	if (t >= TERRAIN_MINE_FIRST && t <= TERRAIN_MINE_LAST) {
		t = (uint8_t)(t - TERRAIN_MINE_ADD);
		mine = 1;
	}
	switch (t) {
	case TERRAIN_BUILDING:
	case TERRAIN_HALFBUILDING:
		game_set_terrain(mx, my, g_damage(s, DMG_BUILDING, mx, my));
		g_sound(SND_SHOT_BUILDING, mx, my, GAME_NO_PLAYER);
		break;
	case TERRAIN_FOREST:
		game_set_terrain(mx, my, mine ? TERRAIN_MINE_GRASS : TERRAIN_GRASS);
		g_sound(SND_SHOT_TREE, mx, my, GAME_NO_PLAYER);
		break;
	case TERRAIN_BOAT:
		game_set_terrain(mx, my, TERRAIN_RIVER);
		g_sound(SND_SHOT_BUILDING, mx, my, GAME_NO_PLAYER);
		break;
	case TERRAIN_GRASS:
	case TERRAIN_SWAMP:
	case TERRAIN_RUBBLE:
		if (mine)
			break;
		nt = g_damage(s, t == TERRAIN_GRASS ? DMG_GRASS :
			t == TERRAIN_SWAMP ? DMG_SWAMP : DMG_RUBBLE, mx, my);
		game_set_terrain(mx, my, nt);
		if (nt == TERRAIN_RIVER)
			g_flood(s, mx, my);
		break;
	case TERRAIN_ROAD:
		if (!mine) {
			nt = check_road(s, mx, my, h->angle);
			game_set_terrain(mx, my, nt);
			if (nt == TERRAIN_RIVER)
				g_sound(SND_SHOT_BUILDING, mx, my, GAME_NO_PLAYER);
		}
		break;
	}
	return 1;
}

// shellsUpdate. A shell that hits or runs out explodes and may set off a
// mine. WinBolo keeps a dead shell one more tick for the network; here it
// goes at once.
void SEC_SHELLS shells_update(GameState *s)
{
	uint8_t i, n = 0;

	for (i = 0; i < s->numShells; i++) {
		Shell *h = &s->shells[i];
		uint16_t wx, wy;
		uint8_t hit = 0;

		if (h->length > 0) {
			int16_t m;

			// shellAdvance1Tick: 24.8 fixed point, the fraction kept 0..255.
			h->accX = (int16_t)(h->accX + h->stepX);
			h->accY = (int16_t)(h->accY + h->stepY);
			m = (int16_t)(h->accX >> 8);
			h->accX = (int16_t)(h->accX - m * 256);
			wx = (uint16_t)(h->x + m);
			m = (int16_t)(h->accY >> 8);
			h->accY = (int16_t)(h->accY - m * 256);
			wy = (uint16_t)(h->y + m);
			hit = collide(s, h, &wx, &wy);
			if (!hit) {
				h->length--;
				h->x = wx;
				h->y = wy;
				s->shells[n++] = *h;
				continue;
			}
		} else {
			wx = h->x;
			wy = h->y;
		}
		g_explosion(s, wx, wy);
		if (hit != 2)
			g_mine_exp(s, (uint8_t)(wx >> 8), (uint8_t)(wy >> 8));
		lgm_blast(s, wx, wy);
	}
	s->numShells = n;
}
