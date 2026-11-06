// Map rules: terrain values, the pill, base and start tables, and the .map
// file parser. Plain C, integers only, no Palm headers.
//
// The terrain values, the file format and the parser follow WinBolo 2
// (src/bolo/bolo_map.c, global.h), Copyright (c) 1998-2026 John Morrison,
// GPLv3. This version Copyright (c) 2026 the PalmBolo authors, GPLv3.

#ifndef MAP_H
#define MAP_H

#include <stdint.h>

// The parser runs once a game, so it goes in the Palm's "cold" segment.
#ifndef SEC_COLD
#ifdef __palmos__
#define SEC_COLD __attribute__((section("cold")))
#else
#define SEC_COLD
#endif
#endif

#define MAP_SIZE        256
#define MAP_MAX_PILLS   16
#define MAP_MAX_BASES   16
#define MAP_MAX_STARTS  16
#define MAP_NEUTRAL     255     // owner of a pill or base nobody has

// Terrain values as stored in the map. Mines add 8 to the plain terrain.
enum {
	TERRAIN_BUILDING = 0,
	TERRAIN_RIVER = 1,
	TERRAIN_SWAMP = 2,
	TERRAIN_CRATER = 3,
	TERRAIN_ROAD = 4,
	TERRAIN_FOREST = 5,
	TERRAIN_RUBBLE = 6,
	TERRAIN_GRASS = 7,
	TERRAIN_HALFBUILDING = 8,
	TERRAIN_BOAT = 9,
	TERRAIN_MINE_SWAMP = 10,
	TERRAIN_MINE_CRATER = 11,
	TERRAIN_MINE_ROAD = 12,
	TERRAIN_MINE_FOREST = 13,
	TERRAIN_MINE_RUBBLE = 14,
	TERRAIN_MINE_GRASS = 15,
	TERRAIN_DEEP_SEA = 255
};
#define TERRAIN_MINE_FIRST  10
#define TERRAIN_MINE_LAST   15
#define TERRAIN_MINE_ADD    8

// Pills, bases and starts keep their file order and numbers for the whole
// game. One in the mined border is marked inactive rather than removed, as
// WinBolo does.
typedef struct MapPill {
	uint8_t x, y;
	uint8_t owner;      // 0-15, or MAP_NEUTRAL
	uint8_t armour;
	uint8_t speed;      // ticks between shots
	uint8_t reload;     // ticks since the last shot, up to speed
	uint8_t coolDown;   // ticks until speed eases back by one
	uint8_t inTank;     // carried by its owner's tank
	uint8_t justSeen;   // had a target last time it was ready
	uint8_t active;
} MapPill;

typedef struct MapBase {
	uint8_t x, y;
	uint8_t owner;      // 0-15, or MAP_NEUTRAL
	uint8_t armour, shells, mines;
	uint8_t active, pad;
} MapBase;

typedef struct MapStart {
	uint8_t x, y;
	uint8_t dir;        // towards land, 0-15
	uint8_t active;
} MapStart;

// Everything about a map except the terrain, which is in the map store.
typedef struct MapInfo {
	uint8_t numPills, numBases, numStarts;
	MapPill pills[MAP_MAX_PILLS];
	MapBase bases[MAP_MAX_BASES];
	MapStart starts[MAP_MAX_STARTS];
} MapInfo;

// Error codes from map_parse.
enum {
	MAP_OK = 0,
	MAP_ERR_SHORT,      // file ends early
	MAP_ERR_HEADER,     // not a BMAPBOLO file
	MAP_ERR_VERSION,    // version other than 1
	MAP_ERR_COUNTS,     // too many pills, bases or starts
	MAP_ERR_RUN,        // bad run header
	MAP_ERR_DATA        // run data doesn't fit its header or the map
};

// Where the file bytes come from. fill copies up to max bytes into dst and
// returns how many it copied, 0 at the end. buf is the parser's own buffer.
typedef struct MapSource {
	uint32_t (*fill)(void *ctx, uint8_t *dst, uint32_t max);
	void *ctx;
	uint8_t buf[64];
	uint16_t pos, n;
	uint8_t eof;
} MapSource;

// A source reading len bytes from memory. ms->ctx points at a MapMemory.
typedef struct MapMemory {
	const uint8_t *data;
	uint32_t len, pos;
} MapMemory;
void SEC_COLD map_source_memory(MapSource *ms, MapMemory *mm, const uint8_t *data, uint32_t len);

// Reads a .map file into info and the map store. The store is cleared to
// deep sea first. As in WinBolo the map is then centred and pills, bases
// and starts in the mined border are marked inactive. Terrain under active
// bases becomes road, and under active pills any water or building becomes
// road and any mine is removed. Returns MAP_OK or
// an error code, and never reads or writes outside the arrays. On an error
// the contents are unspecified (info is zeroed).
int SEC_COLD map_parse(MapInfo *info, MapSource *ms);

const char *SEC_COLD map_error_text(int err);

#endif
