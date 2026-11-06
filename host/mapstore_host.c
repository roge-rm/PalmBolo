// Map storage on the host: a plain 64 KB array.
#include <string.h>
#include "../src/game/mapstore.h"

static uint8_t gMap[256][256];  // [y][x]
static uint8_t gFlag[MS_PLANES][8192];  // bit x & 7 of [y * 32 + x / 8]

const uint8_t *mapstore_rows[256];

int mapstore_open(uint8_t value)
{
	int y;

	for (y = 0; y < 256; y++)
		mapstore_rows[y] = gMap[y];
	memset(gMap, value, sizeof(gMap));
	memset(gFlag, 0, sizeof(gFlag));
	return 0;
}

void mapstore_close(void) {}

void mapstore_fill(uint8_t value)
{
	memset(gMap, value, sizeof(gMap));
}

void mapstore_set(uint8_t x, uint8_t y, uint8_t terrain)
{
	gMap[y][x] = terrain;
}

void mapstore_set_bytes(uint8_t x, uint8_t y, uint16_t n, const uint8_t *src)
{
	if (x + n > 256)
		n = (uint16_t)(256 - x);
	memcpy(&gMap[y][x], src, n);
}

uint8_t mapstore_flag(uint8_t plane, uint8_t x, uint8_t y)
{
	return (uint8_t)((gFlag[plane][y * 32 + (x >> 3)] >> (x & 7)) & 1);
}

void mapstore_set_flag(uint8_t plane, uint8_t x, uint8_t y, uint8_t on)
{
	uint8_t *b = &gFlag[plane][y * 32 + (x >> 3)];

	if (on)
		*b = (uint8_t)(*b | (1 << (x & 7)));
	else
		*b = (uint8_t)(*b & ~(1 << (x & 7)));
}

const uint8_t *mapstore_flags(uint8_t plane)
{
	return gFlag[plane];
}

void mapstore_clear_flags(void)
{
	memset(gFlag, 0, sizeof(gFlag));
}

// The saved game: a copy of the arrays.
static uint8_t gSaveMap[256][256];
static uint8_t gSaveFlag[MS_PLANES][8192];
static int gHaveSave;

int mapstore_save(void)
{
	memcpy(gSaveMap, gMap, sizeof(gMap));
	memcpy(gSaveFlag, gFlag, sizeof(gFlag));
	gHaveSave = 1;
	return 0;
}

int mapstore_resume(void)
{
	if (!gHaveSave)
		return 1;
	memcpy(gMap, gSaveMap, sizeof(gMap));
	memcpy(gFlag, gSaveFlag, sizeof(gFlag));
	return 0;
}

int mapstore_have_save(void)
{
	return gHaveSave;
}

void mapstore_drop_save(void)
{
	gHaveSave = 0;
}
