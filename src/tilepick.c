// Picks the art cell for a map square from its neighbours. The choices
// follow WinBolo 2 (src/bolo/screencalc.c), Copyright (c) 1998-2026 John
// Morrison, GPLv3, including the order the cases are tried in, which
// decides odd shapes. This version Copyright (c) 2026 the PalmBolo authors,
// GPLv3.
#include "tilepick.h"
#include "game/map.h"
#include "art.h"

#define BLD     TERRAIN_BUILDING
#define RIV     TERRAIN_RIVER
#define RD      TERRAIN_ROAD
#define FOR     TERRAIN_FOREST
#define DEEP    TERRAIN_DEEP_SEA

void SEC_TILEPICK tile_neighbours(const uint8_t *above, const uint8_t *row,
	const uint8_t *below, uint8_t x, uint8_t n[8])
{
	uint8_t l = (uint8_t)(x - 1), r = (uint8_t)(x + 1), i;

	n[TP_AL] = above[l];
	n[TP_A] = above[x];
	n[TP_AR] = above[r];
	n[TP_L] = row[l];
	n[TP_R] = row[r];
	n[TP_BL] = below[l];
	n[TP_B] = below[x];
	n[TP_BR] = below[r];
	for (i = 0; i < 8; i++)
		if (n[i] >= TERRAIN_MINE_FIRST && n[i] <= TERRAIN_MINE_LAST)
			n[i] = (uint8_t)(n[i] - TERRAIN_MINE_ADD);
}

// Deep sea and boats count as river for roads, rivers and boats.
static void SEC_TILEPICK water_as_river(const uint8_t n[8], uint8_t m[8])
{
	uint8_t i;

	for (i = 0; i < 8; i++)
		m[i] = (n[i] == DEEP || n[i] == TERRAIN_BOAT) ? RIV : n[i];
}

static uint8_t SEC_TILEPICK pick_road(const uint8_t n0[8])
{
	uint8_t n[8], al, a, ar, l, r, bl, b, br;

	water_as_river(n0, n);
	al = n[TP_AL]; a = n[TP_A]; ar = n[TP_AR]; l = n[TP_L];
	r = n[TP_R]; bl = n[TP_BL]; b = n[TP_B]; br = n[TP_BR];
	if (al != RD && a == RD && ar != RD && l == RD && r == RD && bl != RD && b == RD && br != RD)
		return AT_ROAD_CROSS;
	if (a == RD && l == RD && r == RD && b == RD)
		return AT_ROAD_SOLID;
	if (l == RIV && r == RIV && a == RIV && b == RIV)
		return AT_ROAD_WATER11;
	if (r == RD && b == RD && l == RIV && a == RIV)
		return AT_ROAD_WATER5;
	if (l == RD && b == RD && r == RIV && a == RIV)
		return AT_ROAD_WATER6;
	if (a == RD && l == RD && b == RIV && r == RIV)
		return AT_ROAD_WATER7;
	if (r == RD && a == RD && l == RIV && b == RIV)
		return AT_ROAD_WATER8;
	if (a == RIV && b == RIV)
		return AT_ROAD_WATER9;
	if (l == RIV && r == RIV)
		return AT_ROAD_WATER10;
	if (a == RIV && b == RD)
		return AT_ROAD_WATER1;
	if (r == RIV && l == RD)
		return AT_ROAD_WATER2;
	if (b == RIV && a == RD)
		return AT_ROAD_WATER3;
	if (l == RIV && r == RD)
		return AT_ROAD_WATER4;
	if (r == RD && b == RD && a == RD && (ar == RD || br == RD))
		return AT_ROAD_SIDE1;
	if (l == RD && r == RD && b == RD && (bl == RD || br == RD))
		return AT_ROAD_SIDE2;
	if (l == RD && a == RD && b == RD && (bl == RD || al == RD))
		return AT_ROAD_SIDE3;
	if (l == RD && r == RD && a == RD && (ar == RD || al == RD))
		return AT_ROAD_SIDE4;
	if (l == RD && r == RD && b == RD)
		return AT_ROAD_T1;
	if (l == RD && a == RD && b == RD)
		return AT_ROAD_T2;
	if (l == RD && r == RD && a == RD)
		return AT_ROAD_T3;
	if (r == RD && a == RD && b == RD)
		return AT_ROAD_T4;
	if (b == RD && r == RD && br == RD)
		return AT_ROAD_CORNER5;
	if (b == RD && l == RD && bl == RD)
		return AT_ROAD_CORNER6;
	if (a == RD && l == RD && al == RD)
		return AT_ROAD_CORNER7;
	if (a == RD && r == RD && ar == RD)
		return AT_ROAD_CORNER8;
	if (b == RD && r == RD)
		return AT_ROAD_CORNER1;
	if (b == RD && l == RD)
		return AT_ROAD_CORNER2;
	if (a == RD && l == RD)
		return AT_ROAD_CORNER3;
	if (a == RD && r == RD)
		return AT_ROAD_CORNER4;
	if (r == RD || l == RD)
		return AT_ROAD_HORZ;
	if (a == RD || b == RD)
		return AT_ROAD_VERT;
	return AT_ROAD_SOLID;
}

static uint8_t SEC_TILEPICK pick_boat(const uint8_t n0[8])
{
	uint8_t n[8];

	water_as_river(n0, n);
	if (n[TP_A] != RIV && n[TP_L] != RIV)
		return AT_BOAT_5;
	if (n[TP_A] != RIV && n[TP_R] != RIV)
		return AT_BOAT_6;
	if (n[TP_B] != RIV && n[TP_R] != RIV)
		return AT_BOAT_7;
	if (n[TP_B] != RIV && n[TP_L] != RIV)
		return AT_BOAT_4;
	if (n[TP_L] != RIV)
		return AT_BOAT_2;
	if (n[TP_R] != RIV)
		return AT_BOAT_3;
	if (n[TP_B] != RIV)
		return AT_BOAT_0;
	return AT_BOAT_1;
}

// Neighbour bits for the building and forest choices.
#define B_AL    0x01
#define B_A     0x02
#define B_AR    0x04
#define B_L     0x08
#define B_R     0x10
#define B_BL    0x20
#define B_B     0x40
#define B_BR    0x80

// True if all of the bits in on are set and none of those in off.
#define HAS(on, off)    ((m & ((on) | (off))) == (on))

static uint8_t SEC_TILEPICK pick_building(const uint8_t n[8])
{
	uint8_t m = 0, i;

	for (i = 0; i < 8; i++)
		if (n[i] == BLD || n[i] == TERRAIN_HALFBUILDING)
			m |= (uint8_t)(1 << i);
	if (m == 0xFF)
		return AT_BUILD_SOLID;
	if (HAS(B_R | B_A | B_B | B_L, B_AR | B_AL | B_BR | B_BL))
		return AT_BUILD_CROSS;
	if (HAS(B_R | B_A | B_B | B_L | B_BL, B_AR | B_AL | B_BR))
		return AT_BUILD_MOST1;
	if (HAS(B_R | B_A | B_B | B_L | B_AL, B_AR | B_BR | B_BL))
		return AT_BUILD_MOST2;
	if (HAS(B_R | B_A | B_B | B_L | B_BR, B_AR | B_AL | B_BL))
		return AT_BUILD_MOST3;
	if (HAS(B_R | B_A | B_B | B_L | B_AR, B_AL | B_BR | B_BL))
		return AT_BUILD_MOST4;
	if (HAS(B_AL | B_A | B_L | B_R | B_BL | B_B | B_BR, 0))
		return AT_BUILD_SIDECORN13;
	if (HAS(B_A | B_AR | B_L | B_R | B_BL | B_B | B_BR, 0))
		return AT_BUILD_SIDECORN14;
	if (HAS(B_AL | B_A | B_AR | B_L | B_R | B_BL | B_B, 0))
		return AT_BUILD_SIDECORN15;
	if (HAS(B_AL | B_A | B_AR | B_L | B_R | B_B | B_BR, 0))
		return AT_BUILD_SIDECORN16;
	if (HAS(B_L | B_R | B_A | B_B | B_AR | B_BL, B_AL | B_BR))
		return AT_BUILD_TWIST1;
	if (HAS(B_L | B_R | B_A | B_B | B_BR | B_AL, B_AR | B_BL))
		return AT_BUILD_TWIST2;
	if (HAS(B_A | B_L | B_R | B_B | B_BR | B_AR, 0))
		return AT_BUILD_SIDECORN5;
	if (HAS(B_A | B_L | B_R | B_B | B_BL | B_AL, 0))
		return AT_BUILD_SIDECORN6;
	if (HAS(B_A | B_L | B_R | B_B | B_BL | B_BR, 0))
		return AT_BUILD_SIDECORN7;
	if (HAS(B_A | B_L | B_R | B_B | B_AL | B_AR, 0))
		return AT_BUILD_SIDECORN8;
	if (HAS(B_A | B_B | B_L | B_BL, B_R | B_AL))
		return AT_BUILD_SIDECORN9;
	if (HAS(B_A | B_B | B_R | B_BR, B_L | B_AR))
		return AT_BUILD_SIDECORN10;
	if (HAS(B_A | B_B | B_R | B_AR, B_BR))
		return AT_BUILD_SIDECORN11;
	if (HAS(B_A | B_B | B_L | B_AL, B_BL))
		return AT_BUILD_SIDECORN12;
	if (HAS(B_R | B_A | B_L, B_B | B_AL | B_AR))
		return AT_BUILD_T1;
	if (HAS(B_R | B_B | B_L, B_BL | B_BR))
		return AT_BUILD_T2;
	if (HAS(B_R | B_A | B_B, B_AR | B_BR))
		return AT_BUILD_T3;
	if (HAS(B_B | B_A | B_L, B_AL | B_BL))
		return AT_BUILD_T4;
	if (HAS(B_L | B_R | B_A | B_AR, B_AL))
		return AT_BUILD_SIDECORN1;
	if (HAS(B_L | B_R | B_A | B_AL, B_AR))
		return AT_BUILD_SIDECORN2;
	if (HAS(B_L | B_R | B_B | B_BL, B_BR))
		return AT_BUILD_SIDECORN3;
	if (HAS(B_L | B_R | B_B | B_BR, B_A | B_BL))
		return AT_BUILD_SIDECORN4;
	if (HAS(B_R | B_A | B_B, 0))
		return AT_BUILD_SIDE1;
	if (HAS(B_L | B_A | B_B, 0))
		return AT_BUILD_SIDE2;
	if (HAS(B_R | B_L | B_B, 0))
		return AT_BUILD_SIDE3;
	if (HAS(B_R | B_A | B_L, 0))
		return AT_BUILD_SIDE4;
	if (HAS(B_R | B_B | B_BR, 0))
		return AT_BUILD_CORNER1;
	if (HAS(B_L | B_B | B_BL, 0))
		return AT_BUILD_CORNER2;
	if (HAS(B_R | B_A | B_AR, 0))
		return AT_BUILD_CORNER3;
	if (HAS(B_L | B_A | B_AL, 0))
		return AT_BUILD_CORNER4;
	if (HAS(B_R | B_B, 0))
		return AT_BUILD_L1;
	if (HAS(B_L | B_B, 0))
		return AT_BUILD_L2;
	if (HAS(B_R | B_A, 0))
		return AT_BUILD_L3;
	if (HAS(B_L | B_A, 0))
		return AT_BUILD_L4;
	if (HAS(B_L | B_R, 0))
		return AT_BUILD_HORZ;
	if (HAS(B_A | B_B, 0))
		return AT_BUILD_VERT;
	if (m & B_R)
		return AT_BUILD_HORZEND1;
	if (m & B_L)
		return AT_BUILD_HORZEND2;
	if (m & B_B)
		return AT_BUILD_VERTEND1;
	if (m & B_A)
		return AT_BUILD_VERTEND2;
	return AT_BUILD_SINGLE;
}

static uint8_t SEC_TILEPICK pick_river(const uint8_t n0[8])
{
	uint8_t n[8], a, b, l, r;

	water_as_river(n0, n);
	// True where the side is water or road.
	a = n[TP_A] == RIV || n[TP_A] == RD;
	b = n[TP_B] == RIV || n[TP_B] == RD;
	l = n[TP_L] == RIV || n[TP_L] == RD;
	r = n[TP_R] == RIV || n[TP_R] == RD;
	if (!a && !b && !r && !l)
		return AT_RIVER_SURROUND;
	if (!a && !b && r && !l)
		return AT_RIVER_END1;
	if (!a && !b && !r && l)
		return AT_RIVER_END2;
	if (!a && b && !r && !l)
		return AT_RIVER_END3;
	if (a && !b && !r && !l)
		return AT_RIVER_END4;
	if (!a && !l)
		return AT_RIVER_CORN1;
	if (!a && !r)
		return AT_RIVER_CORN2;
	if (!b && !l)
		return AT_RIVER_CORN3;
	if (!b && !r)
		return AT_RIVER_CORN4;
	if (!b && !a)
		return AT_RIVER_SIDE1;
	if (!l && !r)
		return AT_RIVER_SIDE2;
	if (!l)
		return AT_RIVER_ONESIDE1;
	if (!b)
		return AT_RIVER_ONESIDE2;
	if (!r)
		return AT_RIVER_ONESIDE3;
	if (!a)
		return AT_RIVER_ONESIDE4;
	return AT_RIVER_SOLID;
}

static uint8_t SEC_TILEPICK pick_deep(const uint8_t n0[8])
{
	uint8_t n[8], i;

	for (i = 0; i < 8; i++)
		n[i] = n0[i] == TERRAIN_BOAT ? RIV : n0[i];
	if (n[TP_AL] != DEEP && n[TP_A] != DEEP && n[TP_L] != DEEP && n[TP_R] == DEEP && n[TP_B] == DEEP)
		return AT_DEEP_CORN1;
	if (n[TP_AR] != DEEP && n[TP_A] != DEEP && n[TP_R] != DEEP && n[TP_L] == DEEP && n[TP_B] == DEEP)
		return AT_DEEP_CORN2;
	if (n[TP_BR] != DEEP && n[TP_B] != DEEP && n[TP_R] != DEEP && n[TP_L] == DEEP && n[TP_A] == DEEP)
		return AT_DEEP_CORN4;
	if (n[TP_BL] != DEEP && n[TP_B] != DEEP && n[TP_L] != DEEP && n[TP_R] == DEEP && n[TP_A] == DEEP)
		return AT_DEEP_CORN3;
	if (n[TP_L] == RIV && n[TP_R] == DEEP)
		return AT_DEEP_SIDE1;
	if (n[TP_B] == RIV && n[TP_A] == DEEP)
		return AT_DEEP_SIDE2;
	if (n[TP_A] == RIV && n[TP_B] == DEEP)
		return AT_DEEP_SIDE3;
	if (n[TP_R] == RIV && n[TP_L] == DEEP)
		return AT_DEEP_SIDE4;
	return AT_DEEP_SOLID;
}

static uint8_t SEC_TILEPICK pick_forest(const uint8_t n[8])
{
	uint8_t m = 0;

	if (n[TP_A] == FOR)
		m |= B_A;
	if (n[TP_B] == FOR)
		m |= B_B;
	if (n[TP_L] == FOR)
		m |= B_L;
	if (n[TP_R] == FOR)
		m |= B_R;
	switch (m) {
	case B_R | B_B: return AT_FOREST_BR;
	case B_L | B_B: return AT_FOREST_BL;
	case B_A | B_L: return AT_FOREST_AL;
	case B_A | B_R: return AT_FOREST_AR;
	case B_A: return AT_FOREST_ABOVE;
	case B_B: return AT_FOREST_BELOW;
	case B_L: return AT_FOREST_LEFT;
	case B_R: return AT_FOREST_RIGHT;
	case 0: return AT_FOREST_SINGLE;
	}
	return AT_FOREST;
}

uint8_t SEC_TILEPICK tile_pick(uint8_t t, const uint8_t n[8])
{
	switch (t) {
	case TERRAIN_BUILDING: return pick_building(n);
	case TERRAIN_RIVER: return pick_river(n);
	case TERRAIN_SWAMP: return AT_SWAMP;
	case TERRAIN_CRATER: return AT_CRATER;
	case TERRAIN_ROAD: return pick_road(n);
	case TERRAIN_FOREST: return pick_forest(n);
	case TERRAIN_RUBBLE: return AT_RUBBLE;
	case TERRAIN_GRASS: return AT_GRASS;
	case TERRAIN_HALFBUILDING: return AT_HALFBUILDING;
	case TERRAIN_BOAT: return pick_boat(n);
	case TERRAIN_DEEP_SEA: return pick_deep(n);
	}
	// Anything else is drawn as deep sea, as the old placeholder tiles did.
	return AT_DEEP_SOLID;
}

uint8_t SEC_TILEPICK tile_has_variants(uint8_t t)
{
	return t == TERRAIN_BUILDING || t == TERRAIN_RIVER || t == TERRAIN_ROAD ||
		t == TERRAIN_FOREST || t == TERRAIN_BOAT || t == TERRAIN_DEEP_SEA;
}

uint8_t SEC_TILEPICK tile_bases_matter(uint8_t t)
{
	return t == TERRAIN_ROAD || t == TERRAIN_RIVER;
}
