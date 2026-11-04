// Shells: flight, terrain hits and the explosion where they land.
//
// Ported from WinBolo 2 (src/bolo/shells.c), Copyright (c) 1998-2026 John
// Morrison, GPLv3. This version Copyright (c) 2026 the PalmBolo authors,
// GPLv3.
#include "gamepriv.h"
#include "mapstore.h"
#include "util.h"

// shellsAddItem. The shell starts five steps ahead of the tank and 22 world
// units further on each axis. Its life is 1 + shell_life * sightLen / 2 -
// start_add ticks; WinBolo works out shell_life * len in floats and
// truncates, which is 4 * sightLen exactly here.
void shells_add(GameState *s, uint16_t x, uint16_t y, uint16_t angle,
	uint8_t sightLen, uint8_t owner, uint8_t onBoat)
{
	Shell *h;
	int16_t dx, dy, life;
	uint8_t i;

	util_distance(&dx, &dy, angle, R_SHELL_SPEED);
	x = (uint16_t)(x + R_SHELL_START_ADD * dx + (dx >= 0 ? 22 : -22));
	y = (uint16_t)(y + R_SHELL_START_ADD * dy + (dy >= 0 ? 22 : -22));
	life = (int16_t)(1 + (R_SHELL_LIFE / 2) * sightLen - R_SHELL_START_ADD);
	if (life < 0)
		life = 0;
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
	h->length = (uint8_t)life;
	h->owner = owner;
	h->onBoat = onBoat;
}

// mapIsPassable for a shell.
static uint8_t passable(uint8_t t, uint8_t onBoat)
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
static uint8_t check_road(GameState *s, uint8_t x, uint8_t y, uint16_t angle)
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

// shellsCalcCollision for one player: pills stop shells (pill damage comes
// with the pillboxes), a base stops a shell from a boat, and terrain a
// shell can't pass takes the hit.
static uint8_t collide(GameState *s, const Shell *h, uint16_t wx, uint16_t wy)
{
	uint8_t mx = (uint8_t)(wx >> 8), my = (uint8_t)(wy >> 8);
	int8_t p = g_pill_at(s, mx, my);
	uint8_t t, mine = 0, nt;

	if (p >= 0 && s->map.pills[p].armour > 0)
		return 1;
	if (g_base_at(s, mx, my) >= 0)
		return h->onBoat;
	if (passable(mapstore_get(mx, my), h->onBoat))
		return 0;
	t = game_terrain(mx, my);
	if (t >= TERRAIN_MINE_FIRST && t <= TERRAIN_MINE_LAST) {
		t = (uint8_t)(t - TERRAIN_MINE_ADD);
		mine = 1;
	}
	switch (t) {
	case TERRAIN_BUILDING:
	case TERRAIN_HALFBUILDING:
		game_set_terrain(mx, my, g_damage(s, DMG_BUILDING, mx, my));
		break;
	case TERRAIN_FOREST:
		game_set_terrain(mx, my, mine ? TERRAIN_MINE_GRASS : TERRAIN_GRASS);
		break;
	case TERRAIN_BOAT:
		game_set_terrain(mx, my, TERRAIN_RIVER);
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
		if (!mine)
			game_set_terrain(mx, my, check_road(s, mx, my, h->angle));
		break;
	}
	return 1;
}

// shellsUpdate. A shell that hits or runs out explodes and may set off a
// mine. WinBolo keeps a dead shell one more tick for the network; here it
// goes at once.
void shells_update(GameState *s)
{
	uint8_t i, n = 0;

	for (i = 0; i < s->numShells; i++) {
		Shell *h = &s->shells[i];
		uint16_t wx, wy;

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
			if (!collide(s, h, wx, wy)) {
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
		g_mine_exp(s, (uint8_t)(wx >> 8), (uint8_t)(wy >> 8));
	}
	s->numShells = n;
}
