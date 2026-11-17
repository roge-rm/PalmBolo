// Map editor rules on the host: the save checks, the playable area, the
// terrain list, and that a painted and saved map loads and plays with bots.
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/game/game.h"
#include "../src/game/mapstore.h"
#include "../src/game/mapedit.h"

static uint8_t gFile[70000], gOut[70000];
static uint32_t gFileLen, gOutLen;

// The whole-map copy for big undo steps; gSnapFail plays a Palm short of
// storage.
static uint8_t gSnap[256][256];
static int gSnapFail;

int map_snap_alloc(void)
{
	return gSnapFail;
}

void map_snap_put(uint8_t y, const uint8_t *row)
{
	memcpy(gSnap[y], row, 256);
}

void map_snap_set(uint8_t x, uint8_t y, uint8_t t)
{
	gSnap[y][x] = t;
}

const uint8_t *map_snap_row(uint8_t y)
{
	return gSnap[y];
}

static uint8_t gWas[256][256];

static void remember(void)
{
	int y;

	for (y = 0; y < 256; y++)
		memcpy(gWas[y], mapstore_row(y), 256);
}

static int same(void)
{
	int y;

	for (y = 0; y < 256; y++)
		if (memcmp(gWas[y], mapstore_row(y), 256) != 0)
			return 0;
	return 1;
}

// Squares outside the playable area that aren't deep sea.
static int outside_land(void)
{
	int x, y, n = 0;

	for (y = 0; y < 256; y++)
		for (x = 0; x < 256; x++)
			if (!map_playable((uint8_t)x, (uint8_t)y) && mapstore_get(x, y) != TERRAIN_DEEP_SEA)
				n++;
	return n;
}

static int count_of(uint8_t t)
{
	int x, y, n = 0;

	for (y = 0; y < 256; y++)
		for (x = 0; x < 256; x++)
			n += mapstore_get(x, y) == t;
	return n;
}

static int put(void *ctx, const uint8_t *src, uint32_t n)
{
	(void)ctx;
	if (gOutLen + n > sizeof(gOut))
		return 1;
	memcpy(gOut + gOutLen, src, n);
	gOutLen += n;
	return 0;
}

static int parse(MapInfo *info, const uint8_t *d, uint32_t n)
{
	MapSource ms;
	MapMemory mm;

	map_source_memory(&ms, &mm, d, n);
	return map_parse(info, &ms);
}

static void everard(MapInfo *info)
{
	mapstore_open(TERRAIN_DEEP_SEA);
	assert(parse(info, gFile, gFileLen) == MAP_OK);
}

static void checks(void)
{
	static MapInfo info;
	uint8_t x, y;
	int i;

	everard(&info);
	assert(map_check(&info, &x, &y) == MAPCHK_OK);
	assert(x == 255 && y == 255);
	// Starts sit at sea on the built-in map, which is allowed.
	assert(mapstore_get(info.starts[0].x, info.starts[0].y) == TERRAIN_DEEP_SEA);

	info.numStarts = 0;
	assert(map_check(&info, &x, &y) == MAPCHK_NO_START);
	everard(&info);
	info.numPills = MAP_MAX_PILLS + 1;
	assert(map_check(&info, &x, &y) == MAPCHK_COUNTS);

	everard(&info);
	info.pills[3].x = 20;
	assert(map_check(&info, &x, &y) == MAPCHK_OUTSIDE && x == 20 && y == info.pills[3].y);
	everard(&info);
	info.bases[2].y = 236;
	assert(map_check(&info, &x, &y) == MAPCHK_OUTSIDE && x == info.bases[2].x && y == 236);
	everard(&info);
	info.starts[5].x = 240;
	assert(map_check(&info, &x, &y) == MAPCHK_OUTSIDE && x == 240);

	everard(&info);
	mapstore_set(info.pills[4].x, info.pills[4].y, TERRAIN_DEEP_SEA);
	assert(map_check(&info, &x, &y) == MAPCHK_SEA && x == info.pills[4].x && y == info.pills[4].y);
	everard(&info);
	mapstore_set(info.bases[0].x, info.bases[0].y, TERRAIN_DEEP_SEA);
	assert(map_check(&info, &x, &y) == MAPCHK_SEA && x == info.bases[0].x);
	// Other water is fine under a pill.
	everard(&info);
	mapstore_set(info.pills[4].x, info.pills[4].y, TERRAIN_RIVER);
	assert(map_check(&info, &x, &y) == MAPCHK_OK);

	everard(&info);
	mapstore_set(255, 40, TERRAIN_GRASS);
	assert(map_check(&info, &x, &y) == MAPCHK_EDGE && x == 255 && y == 40);

	for (i = MAPCHK_OK; i <= MAPCHK_WIDE + 1; i++)
		assert(map_check_text(i) != NULL);
}

static void area_and_terrain(void)
{
	int i, j, seen[256] = {0};

	assert(!map_playable(20, 100) && map_playable(21, 100) && map_playable(235, 100));
	assert(!map_playable(236, 100) && !map_playable(100, 20) && !map_playable(100, 236));
	assert(!map_playable(255, 128) && !map_playable(0, 0));
	// Every value the format holds, each once.
	for (i = 0; i < MAP_EDIT_TERRAINS; i++)
		seen[map_edit_terrain((uint8_t)i)]++;
	for (j = 0; j < 16; j++)
		assert(seen[j] == 1);
	assert(seen[TERRAIN_DEEP_SEA] == 1);
	assert(map_edit_terrain(MAP_EDIT_TERRAINS) == TERRAIN_GRASS);
}

// Paints as the editor does, saves, loads the saved bytes and plays them.
static void save_and_play(void)
{
	static MapInfo info, back;
	static GameState g;
	static uint8_t painted[256][256];
	GameSetup st;
	GameInput none[GAME_MAX_TANKS], keys[GAME_MAX_TANKS];
	uint8_t x, y;
	int t, y0;

	everard(&info);
	// A strip of every terrain the picker has, in the sea west of the island
	// and over its edge, and a road across.
	for (y0 = 0; y0 < MAP_EDIT_TERRAINS; y0++)
		for (x = 40; x < 60; x++)
			if (map_playable(x, (uint8_t)(100 + y0)))
				game_set_terrain(x, (uint8_t)(100 + y0), map_edit_terrain((uint8_t)y0));
	for (x = 60; x < 120; x++)
		game_set_terrain(x, 128, TERRAIN_ROAD);
	assert(map_check(&info, &x, &y) == MAPCHK_OK);
	for (t = 0; t < 256; t++)
		memcpy(painted[t], mapstore_row(t), 256);

	gOutLen = 0;
	assert(map_write(&info, put, NULL) == MAP_OK);
	mapstore_open(TERRAIN_DEEP_SEA);
	assert(parse(&back, gOut, gOutLen) == MAP_OK);
	// The painted land still fits the playable area, so the load may move
	// it as a whole; find the shift and check every square.
	{
		int dx, dy, found = 0;

		for (dy = -4; dy <= 4 && !found; dy++)
			for (dx = -4; dx <= 4 && !found; dx++) {
				int ok = 1, yy, xx;

				for (yy = 0; yy < 256 && ok; yy++)
					for (xx = 0; xx < 256 && ok; xx++)
						ok = mapstore_get((uint8_t)(xx + dx), (uint8_t)(yy + dy)) == painted[yy][xx];
				if (ok) {
					found = 1;
					assert(back.numPills == info.numPills && back.numBases == info.numBases);
					assert(back.numStarts == info.numStarts);
					assert(back.pills[0].x == (uint8_t)(info.pills[0].x + dx));
					assert(back.starts[0].y == (uint8_t)(info.starts[0].y + dy));
				}
			}
		assert(found);
	}
	assert(map_check(&back, &x, &y) == MAPCHK_OK);

	memset(&st, 0, sizeof(st));
	st.numPlayers = 1;
	st.numBots = 3;
	st.botLevel = BOT_NORMAL;
	g.map = back;
	game_set_viewer(0);
	game_init(&g, 1234, &st);
	game_set_bot(&g, 0, BOT_NORMAL);
	assert(g.numTanks == 4);
	memset(none, 0, sizeof(none));
	for (t = 0; t < 3000; t++) {
		game_bot_inputs(&g, none, keys);
		game_tick(&g, keys);
	}
	assert(g.tick == 3000);
	for (t = 0; t < g.numTanks; t++)
		assert(g.tanks[t].x >> 8 > 0 && g.tanks[t].y >> 8 > 0);
}

// Places, moves and removes items, with the rules for where they go.
static void items(void)
{
	static MapInfo info;
	static MapUndo u;
	uint8_t k, i, x, y, px, py, n;
	int err;

	everard(&info);
	memset(&u, 0, sizeof(u));
	// Everard's pill 0 is found where it is, and an empty square has none.
	assert(map_item_at(&info, info.pills[0].x, info.pills[0].y, &k, &i) && k == ITEM_PILL && i == 0);
	assert(map_item_at(&info, info.bases[2].x, info.bases[2].y, &k, &i) && k == ITEM_BASE && i == 2);
	assert(map_item_at(&info, info.starts[1].x, info.starts[1].y, &k, &i) && k == ITEM_START && i == 1);
	assert(!map_item_at(&info, 30, 30, &k, &i));

	// Full lists: Everard has 16 pills and starts and 11 bases.
	assert(info.numPills == 16 && info.numStarts == 16 && info.numBases == 11);
	assert(map_item_add(&u, &info, ITEM_PILL, 128, 128, &i) == MAPITEM_FULL);
	assert(map_item_add(&u, &info, ITEM_START, 30, 30, &i) == MAPITEM_FULL);
	assert(strstr(map_item_text(MAPITEM_FULL, ITEM_PILL), "pills"));

	// A base on land: road under it, neutral and full.
	x = info.pills[0].x;
	y = (uint8_t)(info.pills[0].y + 1);
	while (map_item_at(&info, x, y, &k, &i) || mapstore_get(x, y) == TERRAIN_DEEP_SEA)
		y++;
	mapstore_set(x, y, TERRAIN_GRASS);
	n = info.numBases;
	assert(map_item_add(&u, &info, ITEM_BASE, x, y, &i) == MAPITEM_OK && i == n);
	assert(mapstore_get(x, y) == TERRAIN_GRASS);
	map_item_ground(&u, &info, ITEM_BASE, i);
	assert(info.numBases == n + 1 && info.bases[i].x == x && info.bases[i].y == y);
	assert(info.bases[i].owner == MAP_NEUTRAL && info.bases[i].armour == 90 &&
		info.bases[i].shells == 90 && info.bases[i].mines == 90 && info.bases[i].active);
	assert(mapstore_get(x, y) == TERRAIN_ROAD);

	// Two items on one square, deep sea and the border are refused.
	assert(map_item_add(&u, &info, ITEM_BASE, info.pills[0].x, info.pills[0].y, &i) == MAPITEM_TAKEN);
	assert(map_item_add(&u, &info, ITEM_BASE, 30, 30, &i) == MAPITEM_SEA);
	assert(map_item_add(&u, &info, ITEM_BASE, 20, 128, &i) == MAPITEM_OUTSIDE);
	assert(map_item_add(&u, &info, ITEM_BASE, 128, 236, &i) == MAPITEM_OUTSIDE);
	assert(map_item_fits(&info, ITEM_START, 30, 30, ITEM_NONE, ITEM_NONE) == MAPITEM_OK);
	for (err = MAPITEM_TAKEN; err <= MAPITEM_FULL; err++)
		assert(map_item_text(err, ITEM_BASE)[0]);

	// Moving: onto its own square is fine, onto another item or the sea not.
	px = info.pills[3].x;
	py = info.pills[3].y;
	assert(map_item_move(&u, &info, ITEM_PILL, 3, px, py) == MAPITEM_OK);
	assert(map_item_move(&u, &info, ITEM_PILL, 3, info.pills[4].x, info.pills[4].y) == MAPITEM_TAKEN);
	assert(map_item_move(&u, &info, ITEM_PILL, 3, 30, 30) == MAPITEM_SEA);
	assert(map_item_move(&u, &info, ITEM_PILL, 3, 236, 100) == MAPITEM_OUTSIDE);
	assert(info.pills[3].x == px && info.pills[3].y == py);
	// A start may go to sea.
	assert(map_item_move(&u, &info, ITEM_START, 0, 30, 30) == MAPITEM_OK);
	assert(info.starts[0].x == 30 && info.starts[0].y == 30);
	// A pill moved onto river gets road under it on the drop; mines go.
	mapstore_set(x, (uint8_t)(y + 1), TERRAIN_RIVER);
	if (!map_item_at(&info, x, (uint8_t)(y + 1), &k, &i)) {
		assert(map_item_move(&u, &info, ITEM_PILL, 3, x, (uint8_t)(y + 1)) == MAPITEM_OK);
		assert(mapstore_get(x, (uint8_t)(y + 1)) == TERRAIN_RIVER);
		map_item_ground(&u, &info, ITEM_PILL, 3);
		assert(mapstore_get(x, (uint8_t)(y + 1)) == TERRAIN_ROAD);
	}
	// Painting under items keeps their ground.
	assert(map_paint(&u, &info, x, y, TERRAIN_FOREST) == 0);
	assert(mapstore_get(x, y) == TERRAIN_ROAD);
	px = info.pills[5].x;
	py = info.pills[5].y;
	map_paint(&u, &info, px, py, TERRAIN_MINE_FOREST);
	assert(mapstore_get(px, py) == TERRAIN_FOREST);
	map_paint(&u, &info, px, py, TERRAIN_BUILDING);
	assert(mapstore_get(px, py) == TERRAIN_ROAD);
	map_paint(&u, &info, px, py, TERRAIN_SWAMP);
	assert(mapstore_get(px, py) == TERRAIN_SWAMP);

	// Removing keeps the order of the rest.
	px = info.pills[7].x;
	py = info.pills[7].y;
	map_item_remove(&u, &info, ITEM_PILL, 6);
	assert(info.numPills == 15 && info.pills[6].x == px && info.pills[6].y == py);
	map_item_remove(&u, &info, ITEM_PILL, 20);
	assert(info.numPills == 15);
	// Now there's room for a pill, which shows full armour.
	assert(map_item_add(&u, &info, ITEM_PILL, info.starts[0].x, info.starts[0].y, &i) == MAPITEM_TAKEN);
	mapstore_set(x, (uint8_t)(y - 1), TERRAIN_GRASS);
	if (!map_item_at(&info, x, (uint8_t)(y - 1), &k, &i)) {
		assert(map_item_add(&u, &info, ITEM_PILL, x, (uint8_t)(y - 1), &i) == MAPITEM_OK);
		assert(i == 15 && info.pills[15].armour == 15 && info.pills[15].owner == MAP_NEUTRAL);
	}

	// Starts face the middle of the map.
	map_item_remove(&u, &info, ITEM_START, 15);
	assert(map_item_add(&u, &info, ITEM_START, 40, 128, &i) == MAPITEM_OK && info.starts[i].dir == 0);
	map_item_remove(&u, &info, ITEM_START, i);
	assert(map_item_add(&u, &info, ITEM_START, 128, 40, &i) == MAPITEM_OK && info.starts[i].dir == 12);
	map_item_remove(&u, &info, ITEM_START, i);
	assert(map_item_add(&u, &info, ITEM_START, 220, 128, &i) == MAPITEM_OK && info.starts[i].dir == 8);
	map_item_remove(&u, &info, ITEM_START, i);
	assert(map_item_add(&u, &info, ITEM_START, 128, 220, &i) == MAPITEM_OK && info.starts[i].dir == 4);
	map_item_remove(&u, &info, ITEM_START, i);
	assert(map_item_add(&u, &info, ITEM_START, 40, 40, &i) == MAPITEM_OK && info.starts[i].dir == 14);

	// A map with items moved about still saves and loads.
	assert(map_check(&info, &x, &y) == MAPCHK_OK);
	gOutLen = 0;
	assert(map_write(&info, put, NULL) == MAP_OK);
}

// One step of undo for a stroke and for an item change.
static void undo(void)
{
	static MapInfo info, before;
	static MapUndo u;
	static uint8_t was[256][256];
	(void)gWas;
	uint8_t x, i;
	int t, n;

	everard(&info);
	for (t = 0; t < 256; t++)
		memcpy(was[t], mapstore_row(t), 256);
	before = info;
	memset(&u, 0, sizeof(u));
	assert(!map_undo(&u, &info));

	// A stroke over a pill and across, then undone.
	map_undo_next(&u);
	for (x = 60; x < 120; x++)
		map_paint(&u, &info, x, 128, TERRAIN_SWAMP);
	map_paint(&u, &info, info.pills[0].x, info.pills[0].y, TERRAIN_RIVER);
	map_paint(&u, &info, 70, 128, TERRAIN_SWAMP);
	assert(mapstore_get(70, 128) == TERRAIN_SWAMP);
	// A tap that changes nothing, or a refused move, keeps the step.
	n = u.n;
	assert(u.on && n > 50);
	map_undo_next(&u);
	assert(!map_paint(&u, &info, 70, 128, TERRAIN_SWAMP));
	assert(map_item_move(&u, &info, ITEM_PILL, 0, 30, 30) == MAPITEM_SEA);
	assert(u.on && u.n == n);
	assert(map_undo(&u, &info));
	for (t = 0; t < 256; t++)
		assert(memcmp(was[t], mapstore_row(t), 256) == 0);
	// Only one step.
	assert(!map_undo(&u, &info));

	// Item changes: a move, a removal and a new base with its road.
	map_undo_next(&u);
	map_item_move(&u, &info, ITEM_START, 2, 30, 30);
	map_item_remove(&u, &info, ITEM_PILL, 0);
	mapstore_set(60, 128, TERRAIN_GRASS);
	for (t = 0; t < 256; t++)
		memcpy(was[t], mapstore_row(t), 256);
	before = info;
	map_undo_next(&u);
	assert(map_item_add(&u, &info, ITEM_BASE, 60, 128, &i) == MAPITEM_OK);
	map_item_ground(&u, &info, ITEM_BASE, i);
	assert(mapstore_get(60, 128) == TERRAIN_ROAD);
	map_item_remove(&u, &info, ITEM_PILL, 1);
	assert(map_undo(&u, &info));
	assert(memcmp(&info, &before, sizeof(info)) == 0);
	for (t = 0; t < 256; t++)
		assert(memcmp(was[t], mapstore_row(t), 256) == 0);

	// A stroke past MAP_UNDO_SQUARES goes on in the whole-map copy.
	everard(&info);
	remember();
	before = info;
	map_undo_next(&u);
	for (t = 0; t < MAP_UNDO_SQUARES + 10; t++)
		map_paint(&u, &info, (uint8_t)(30 + t % 200), (uint8_t)(30 + t / 200), TERRAIN_GRASS);
	assert(u.snap && !u.over);
	assert(map_undo(&u, &info));
	assert(same() && memcmp(&info, &before, sizeof(info)) == 0);
	// Without room for the copy it can't be undone.
	gSnapFail = 1;
	map_undo_next(&u);
	for (t = 0; t < MAP_UNDO_SQUARES + 10; t++)
		map_paint(&u, &info, (uint8_t)(30 + t % 200), (uint8_t)(30 + t / 200), TERRAIN_GRASS);
	assert(u.over);
	assert(!map_undo(&u, &info));
	assert(mapstore_get(30, 30) == TERRAIN_GRASS);
	gSnapFail = 0;
	// A stroke of exactly the most squares can.
	everard(&info);
	map_undo_next(&u);
	for (t = 0; t < MAP_UNDO_SQUARES; t++)
		map_paint(&u, &info, (uint8_t)(30 + t % 200), (uint8_t)(30 + t / 200), TERRAIN_GRASS);
	assert(!u.over && map_undo(&u, &info));
	assert(mapstore_get(30, 30) == TERRAIN_DEEP_SEA);
}


// Each size of new map passes the save checks and comes back the same
// from a save and load, not moved.
static void blank(void)
{
	static MapInfo info, back;
	uint8_t size, x, y;
	int n, last = 0;

	for (size = 0; size < MAP_BLANK_SIZES; size++) {
		mapstore_open(TERRAIN_GRASS);
		map_blank(&info, size);
		assert(info.numStarts == 1 && info.numPills == 0 && info.numBases == 0);
		assert(info.starts[0].active);
		assert(mapstore_get(info.starts[0].x, info.starts[0].y) == TERRAIN_GRASS);
		assert(map_check(&info, &x, &y) == MAPCHK_OK);
		assert(outside_land() == 0);
		n = count_of(TERRAIN_GRASS);
		assert(n > last && n + count_of(TERRAIN_DEEP_SEA) == 65536);
		last = n;
		// Round, so the corners of its box are sea.
		assert(mapstore_get(126, 126) == TERRAIN_GRASS);
		remember();
		gOutLen = 0;
		assert(map_write(&info, put, NULL) == MAP_OK);
		mapstore_open(TERRAIN_DEEP_SEA);
		assert(parse(&back, gOut, gOutLen) == MAP_OK);
		assert(same());
		assert(back.numStarts == 1 && back.starts[0].x == info.starts[0].x &&
			back.starts[0].y == info.starts[0].y && back.starts[0].dir == info.starts[0].dir);
		assert(map_check(&back, &x, &y) == MAPCHK_OK);
	}
	// The large island leaves sea round it inside the border.
	assert(mapstore_get(30, 126) == TERRAIN_DEEP_SEA);
}

static void fill(void)
{
	static MapInfo info, before;
	static MapUndo u;
	uint8_t x, y, i;
	int sea, n, land;

	memset(&u, 0, sizeof(u));
	mapstore_open(TERRAIN_DEEP_SEA);
	map_blank(&info, MAP_BLANK_MEDIUM);
	land = count_of(TERRAIN_GRASS);
	sea = 215 * 215 - land;
	// The same terrain does nothing, nor does the border.
	map_undo_next(&u);
	assert(map_fill(&u, &info, 126, 126, TERRAIN_GRASS) == 0);
	assert(map_fill(&u, &info, 20, 30, TERRAIN_RIVER) == 0);
	assert(!u.on);

	// All the sea, a big area: only inside the border, the island left.
	remember();
	before = info;
	map_undo_next(&u);
	n = map_fill(&u, &info, 30, 30, TERRAIN_RIVER);
	assert(n == sea && count_of(TERRAIN_RIVER) == sea && count_of(TERRAIN_GRASS) == land);
	assert(outside_land() == 0 && count_of(254) == 0);
	assert(u.snap && map_undo(&u, &info));
	assert(same() && memcmp(&info, &before, sizeof(info)) == 0);

	// The island alone, with a base and a pill on it.
	map_undo_next(&u);
	assert(map_item_add(&u, &info, ITEM_BASE, 120, 120, &i) == MAPITEM_OK);
	map_item_ground(&u, &info, ITEM_BASE, i);
	assert(map_item_add(&u, &info, ITEM_PILL, 130, 130, &i) == MAPITEM_OK);
	mapstore_set(120, 121, TERRAIN_FOREST);
	map_undo_next(&u);
	n = map_fill(&u, &info, 126, 126, TERRAIN_RIVER);
	assert(n == land - 2);
	assert(mapstore_get(120, 120) == TERRAIN_ROAD && mapstore_get(130, 130) == TERRAIN_ROAD);
	assert(mapstore_get(120, 121) == TERRAIN_FOREST && mapstore_get(30, 30) == TERRAIN_DEEP_SEA);
	assert(count_of(TERRAIN_RIVER) == land - 3);
	// Under 256 squares it undoes from the list.
	mapstore_open(TERRAIN_DEEP_SEA);
	map_blank(&info, MAP_BLANK_SMALL);
	remember();
	map_undo_next(&u);
	n = map_fill(&u, &info, 126, 126, TERRAIN_FOREST);
	assert(n > 0 && n < MAP_UNDO_SQUARES ? !u.snap : u.snap);
	assert(map_undo(&u, &info) && same());

	// A comb of many teeth runs the stack over; edge to edge only.
	mapstore_open(TERRAIN_DEEP_SEA);
	memset(&info, 0, sizeof(info));
	land = 0;
	for (x = 25; x <= 231; x++) {
		mapstore_set(x, 25, TERRAIN_GRASS);
		land++;
		if (x % 2)
			continue;
		for (y = 26; y <= 230; y++) {
			mapstore_set(x, y, TERRAIN_GRASS);
			land++;
		}
	}
	// Corner to corner doesn't join.
	mapstore_set(232, 24, TERRAIN_GRASS);
	map_undo_next(&u);
	assert(map_fill(&u, &info, 25, 25, TERRAIN_FOREST) == land);
	assert(count_of(TERRAIN_FOREST) == land && count_of(TERRAIN_GRASS) == 1);
	// Right up to the border and no further.
	mapstore_open(TERRAIN_GRASS);
	map_undo_next(&u);
	assert(map_fill(&u, &info, 128, 128, TERRAIN_SWAMP) == 215 * 215);
	assert(mapstore_get(21, 21) == TERRAIN_SWAMP && mapstore_get(235, 235) == TERRAIN_SWAMP);
	assert(mapstore_get(20, 21) == TERRAIN_GRASS && mapstore_get(236, 128) == TERRAIN_GRASS);

	// Without room for the copy a big fill still works, but can't be undone.
	mapstore_open(TERRAIN_DEEP_SEA);
	map_blank(&info, MAP_BLANK_LARGE);
	gSnapFail = 1;
	map_undo_next(&u);
	assert(map_fill(&u, &info, 126, 126, TERRAIN_FOREST) > MAP_UNDO_SQUARES);
	assert(u.over && !map_undo(&u, &info));
	gSnapFail = 0;
}

static void box_and_brush(void)
{
	static MapInfo info, before;
	static MapUndo u;
	uint8_t i;
	int x, y;

	memset(&u, 0, sizeof(u));
	mapstore_open(TERRAIN_DEEP_SEA);
	map_blank(&info, MAP_BLANK_MEDIUM);
	// Cut to the playable area, corners in any order.
	remember();
	map_undo_next(&u);
	assert(map_box(&u, &info, 30, 30, 5, 5, TERRAIN_ROAD) == 100);
	for (y = 0; y < 256; y++)
		for (x = 0; x < 256; x++)
			assert((mapstore_get(x, y) == TERRAIN_ROAD) == (x >= 21 && x <= 30 && y >= 21 && y <= 30));
	assert(!u.snap && map_undo(&u, &info) && same());
	assert(map_box(&u, &info, 250, 0, 255, 255, TERRAIN_ROAD) == 0);
	assert(outside_land() == 0);

	// A big box over items: road under the base, the pill keeps its ground.
	assert(map_item_add(&u, &info, ITEM_BASE, 110, 110, &i) == MAPITEM_OK);
	map_item_ground(&u, &info, ITEM_BASE, i);
	assert(map_item_add(&u, &info, ITEM_PILL, 112, 112, &i) == MAPITEM_OK);
	remember();
	before = info;
	map_undo_next(&u);
	map_box(&u, &info, 100, 100, 160, 140, TERRAIN_DEEP_SEA);
	assert(mapstore_get(110, 110) == TERRAIN_ROAD && mapstore_get(112, 112) == TERRAIN_ROAD);
	assert(mapstore_get(100, 100) == TERRAIN_DEEP_SEA && mapstore_get(160, 140) == TERRAIN_DEEP_SEA);
	assert(mapstore_get(161, 140) == TERRAIN_GRASS);
	assert(u.snap && map_undo(&u, &info) && same());
	assert(memcmp(&info, &before, sizeof(info)) == 0);
	// The whole area.
	map_undo_next(&u);
	map_box(&u, &info, 0, 0, 255, 255, TERRAIN_SWAMP);
	assert(count_of(TERRAIN_SWAMP) == 215 * 215 - 1 && mapstore_get(110, 110) == TERRAIN_ROAD);
	assert(outside_land() == 0);
	assert(map_undo(&u, &info) && same());

	// Brushes at the edges paint only inside.
	remember();
	map_undo_next(&u);
	assert(map_brush(&u, &info, 21, 21, 5, TERRAIN_FOREST) == 9);
	assert(map_brush(&u, &info, 235, 128, 3, TERRAIN_FOREST) == 6);
	assert(map_brush(&u, &info, 255, 255, 5, TERRAIN_FOREST) == 0);
	assert(map_brush(&u, &info, 0, 0, 5, TERRAIN_FOREST) == 0);
	assert(map_brush(&u, &info, 60, 60, 1, TERRAIN_FOREST) == 1);
	assert(map_brush(&u, &info, 60, 60, 5, TERRAIN_FOREST) == 24);
	assert(outside_land() == 0 && count_of(TERRAIN_FOREST) == 9 + 6 + 25);
	assert(map_undo(&u, &info) && same());
}

// Saves as the editor does (map_check, map_settle, map_write), loads the
// bytes and expects the same map in the same place. Returns map_settle's
// result; on a refusal the map must be as it was.
static int settle_trip(MapInfo *info)
{
	static MapInfo back, was;
	MapUndo u;
	uint8_t x, y, dx, dy;
	int err, i;

	memset(&u, 0, sizeof(u));
	u.on = 1;
	assert(map_check(info, &x, &y) == MAPCHK_OK);
	remember();
	was = *info;
	err = map_settle(&u, info, &x, &y, &dx, &dy);
	if (err != MAPCHK_OK) {
		assert(same() && memcmp(&was, info, sizeof(was)) == 0);
		assert(dx == 0 && dy == 0 && u.on == 1);
		return err;
	}
	assert(dx == 0 || dy == 0);
	if (dx || dy)
		assert(u.on == 0);
	assert(map_check(info, &x, &y) == MAPCHK_OK);
	remember();
	gOutLen = 0;
	assert(map_write(info, put, NULL) == MAP_OK);
	mapstore_open(TERRAIN_DEEP_SEA);
	assert(parse(&back, gOut, gOutLen) == MAP_OK);
	assert(same());
	assert(back.numStarts == info->numStarts && back.numPills == info->numPills);
	for (i = 0; i < info->numStarts; i++)
		assert(back.starts[i].x == info->starts[i].x && back.starts[i].y == info->starts[i].y &&
			back.starts[i].active);
	for (i = 0; i < info->numPills; i++)
		assert(back.pills[i].x == info->pills[i].x && back.pills[i].y == info->pills[i].y &&
			back.pills[i].active);
	assert(map_check(&back, &x, &y) == MAPCHK_OK);
	return MAPCHK_OK;
}

// A map the load would centre is centred on save, so it loads where it is.
static void settle(void)
{
	static MapInfo info;
	MapUndo u;
	uint8_t idx, x, y, dx, dy;
	int16_t ax, ay;

	memset(&u, 0, sizeof(u));
	u.next = 1;
	// Land widened west and a start far out at sea: the load would move
	// it 23 squares both ways, the start into the border.
	mapstore_open(TERRAIN_DEEP_SEA);
	map_blank(&info, MAP_BLANK_MEDIUM);
	map_box(&u, &info, 40, 40, 60, 60, TERRAIN_GRASS);
	assert(map_item_add(&u, &info, ITEM_START, 220, 120, &idx) == MAPITEM_OK);
	assert(map_center_move(&info, &ax, &ay) == 1);
	assert(settle_trip(&info) == MAPCHK_OK);
	assert(map_center_move(&info, &ax, &ay) == 0);
	assert(info.starts[1].x == 220);

	// Land over the whole playable area can't be centred either way.
	mapstore_open(TERRAIN_DEEP_SEA);
	map_blank(&info, MAP_BLANK_MEDIUM);
	map_box(&u, &info, 21, 21, 235, 235, TERRAIN_GRASS);
	assert(settle_trip(&info) == MAPCHK_WIDE);
	{
		MapUndo v;

		memset(&v, 0, sizeof(v));
		assert(map_settle(&v, &info, &x, &y, &dx, &dy) == MAPCHK_WIDE && x == 255 && y == 255);
	}
	// Full width but short of the bottom rows: centring down fits.
	mapstore_open(TERRAIN_DEEP_SEA);
	map_blank(&info, MAP_BLANK_MEDIUM);
	map_box(&u, &info, 21, 21, 235, 225, TERRAIN_GRASS);
	assert(settle_trip(&info) == MAPCHK_OK);
	// Full height and width to 232 loads in place without a move.
	mapstore_open(TERRAIN_DEEP_SEA);
	map_blank(&info, MAP_BLANK_MEDIUM);
	map_box(&u, &info, 21, 21, 232, 235, TERRAIN_GRASS);
	assert(map_center_move(&info, &ax, &ay) == 0);
	assert(settle_trip(&info) == MAPCHK_OK);

	// Items that would leave the area either way: the first move's item
	// is named.
	mapstore_open(TERRAIN_DEEP_SEA);
	map_blank(&info, MAP_BLANK_MEDIUM);
	map_box(&u, &info, 30, 30, 50, 50, TERRAIN_GRASS);
	assert(map_item_add(&u, &info, ITEM_START, 230, 230, &idx) == MAPITEM_OK);
	assert(settle_trip(&info) == MAPCHK_FAR);
	{
		MapUndo v;

		memset(&v, 0, sizeof(v));
		assert(map_settle(&v, &info, &x, &y, &dx, &dy) == MAPCHK_FAR && x == 230 && y == 230);
	}
}

int main(void)
{
	FILE *f = fopen("res/everard.map", "rb");

	assert(f);
	gFileLen = (uint32_t)fread(gFile, 1, sizeof(gFile), f);
	fclose(f);
	assert(gFileLen > 0 && gFileLen < sizeof(gFile));
	checks();
	area_and_terrain();
	save_and_play();
	items();
	undo();
	blank();
	fill();
	box_and_brush();
	settle();
	printf("editor tests passed\n");
	return 0;
}
