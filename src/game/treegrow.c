// Trees growing back: each tick one square picked by the growth's own
// generator is scored by its neighbours, and after a quiet spell a tree
// grows on the best one seen.
//
// Ported from WinBolo 2 (src/bolo/treegrow.c), Copyright (c) 1998-2026 John
// Morrison, GPLv3. This version Copyright (c) 2026 the PalmBolo authors,
// GPLv3.
#include "gamepriv.h"

// treeGrowCreate: seeded from the game's generator.
void SEC_BUILDER tree_init(GameState *s)
{
	s->treeSeed = (uint16_t)((uint16_t)game_random(s) | 1);
	s->treeTime = (int16_t)R_TREE_GROW_INITIAL_TICKS;
	s->treeScore = R_TREE_GROW_INITIAL_SCORE;
	s->treeX = 0;
	s->treeY = 0;
}

static uint8_t SEC_BUILDER pill_or_base(const GameState *s, uint8_t x, uint8_t y)
{
	return g_pill_at(s, x, y) >= 0 || g_base_at(s, x, y) >= 0;
}

// treeGrowCalcSquare: one square's weight.
static int16_t SEC_BUILDER square_weight(const GameState *s, uint8_t x, uint8_t y)
{
	int16_t v = 0;
	uint8_t t;

	if (pill_or_base(s, x, y))
		return R_TREE_ROAD;
	t = game_terrain(x, y);
	if (t >= TERRAIN_MINE_FIRST && t <= TERRAIN_MINE_LAST) {
		v = R_TREE_MINE;
		t = (uint8_t)(t - TERRAIN_MINE_ADD);
	}
	switch (t) {
	case TERRAIN_FOREST: return (int16_t)(v + R_TREE_FOREST);
	case TERRAIN_GRASS: return (int16_t)(v + R_TREE_GRASS);
	case TERRAIN_RIVER: return (int16_t)(v + R_TREE_RIVER);
	case TERRAIN_BOAT: return (int16_t)(v + R_TREE_BOAT);
	case TERRAIN_DEEP_SEA: return (int16_t)(v + R_TREE_DEEP_SEA);
	case TERRAIN_SWAMP: return (int16_t)(v + R_TREE_SWAMP);
	case TERRAIN_RUBBLE: return (int16_t)(v + R_TREE_RUBBLE);
	case TERRAIN_BUILDING: return (int16_t)(v + R_TREE_BUILDING);
	case TERRAIN_HALFBUILDING: return (int16_t)(v + R_TREE_HALF_BUILDING);
	case TERRAIN_CRATER: return (int16_t)(v + R_TREE_CRATER);
	}
	return (int16_t)(v + R_TREE_ROAD);
}

// treeGrowCalcScore: the square and its eight neighbours, inside 0-255,
// and never below 0. Forest, river, walls, pills and bases score 0.
static int16_t SEC_BUILDER score(const GameState *s, uint8_t mx, uint8_t my)
{
	int16_t v = 0;
	uint8_t t = game_terrain(mx, my);
	int8_t dx, dy;

	if (t >= TERRAIN_MINE_FIRST && t <= TERRAIN_MINE_LAST)
		t = (uint8_t)(t - TERRAIN_MINE_ADD);
	if (t == TERRAIN_FOREST || t == TERRAIN_RIVER || t == TERRAIN_BUILDING ||
	    t == TERRAIN_HALFBUILDING || pill_or_base(s, mx, my))
		return 0;
	for (dx = -1; dx <= 1; dx++) {
		if ((dx < 0 && mx == 0) || (dx > 0 && mx == 255))
			continue;
		for (dy = -1; dy <= 1; dy++) {
			if ((dy < 0 && my == 0) || (dy > 0 && my == 255))
				continue;
			v = (int16_t)(v + square_weight(s, (uint8_t)(mx + dx), (uint8_t)(my + dy)));
		}
	}
	return v < 0 ? 0 : v;
}

// treeGrowCheckGrowTree.
static void SEC_BUILDER check_grow(GameState *s)
{
	uint8_t pos, x = s->treeX, y = s->treeY;

	s->treeTime--;
	if (s->treeTime > 0)
		return;
	s->treeScore = R_TREE_GROW_INITIAL_SCORE;
	s->treeTime = (int16_t)R_TREE_GROW_INITIAL_TICKS;
	pos = game_terrain(x, y);
	if (pos == TERRAIN_RIVER || pos == TERRAIN_BUILDING || pos == TERRAIN_HALFBUILDING ||
	    pos == TERRAIN_DEEP_SEA || pos == TERRAIN_BOAT || pill_or_base(s, x, y) ||
	    lgm_building_at(s, x, y))
		return;
	game_set_terrain(x, y, (pos >= TERRAIN_MINE_FIRST && pos <= TERRAIN_MINE_LAST) ?
		TERRAIN_MINE_FOREST : TERRAIN_FOREST);
	switch (pos) {
	case TERRAIN_GRASS: g_damage_remove(s, DMG_GRASS, x, y); break;
	case TERRAIN_SWAMP: g_damage_remove(s, DMG_SWAMP, x, y); break;
	case TERRAIN_RUBBLE: g_damage_remove(s, DMG_RUBBLE, x, y); break;
	}
}

// The generator's 16 steps at a time. Each step is linear over bits, so
// 16 steps of a seed are those of its high byte xor those of its low byte,
// in two tables made on first use.
static uint16_t gStepHi[256], gStepLo[256];
static uint8_t gStepReady;

static uint16_t SEC_BUILDER step16(uint16_t rnd)
{
	uint8_t i;

	for (i = 0; i < 16; i++)
		rnd = (uint16_t)((rnd << 1) ^ (rnd & 0x8000 ? 0x1083 : 0));
	return rnd;
}

static void SEC_BUILDER step_init(void)
{
	uint16_t i;

	for (i = 0; i < 256; i++) {
		gStepHi[i] = step16((uint16_t)(i << 8));
		gStepLo[i] = step16(i);
	}
	gStepReady = 1;
}

// treeGrowUpdate.
void SEC_BUILDER tree_update(GameState *s)
{
	uint16_t rnd = s->treeSeed;
	uint8_t x, y;

	if (!gStepReady)
		step_init();
	rnd = (uint16_t)(gStepHi[rnd >> 8] ^ gStepLo[rnd & 0xFF]);
	s->treeSeed = rnd;
	x = (uint8_t)(rnd >> 8);
	y = (uint8_t)rnd;
	if (!pill_or_base(s, x, y) && !g_is_mine(x, y) && game_terrain(x, y) != TERRAIN_DEEP_SEA) {
		int16_t v = score(s, x, y);

		// treeGrowAddItem
		if (v > s->treeScore && v > 0) {
			s->treeScore = v;
			s->treeX = x;
			s->treeY = y;
			s->treeTime = (int16_t)R_TREE_GROW_TICKS;
		}
	}
	check_grow(s);
}
