#include "mapthumb.h"

static const char magic[] = "BMAPBOLO";

static int next(MapSource *s)
{
	if (s->pos == s->n) {
		if (s->eof) return -1;
		s->n = (uint16_t)s->fill(s->ctx, s->buf, sizeof(s->buf));
		s->pos = 0;
		if (!s->n) { s->eof = 1; return -1; }
	}
	return s->buf[s->pos++];
}

static int bytes(MapSource *s, uint8_t *p, int n)
{
	int v;
	while (n--) {
		v = next(s);
		if (v < 0) return 0;
		*p++ = (uint8_t)v;
	}
	return 1;
}

static uint8_t colour(uint8_t terrain)
{
	if (terrain >= TERRAIN_MINE_FIRST) terrain -= TERRAIN_MINE_ADD;
	switch (terrain) {
	case TERRAIN_RIVER: case TERRAIN_BOAT: return THUMB_WATER;
	case TERRAIN_FOREST: return THUMB_FOREST;
	case TERRAIN_ROAD: return THUMB_ROAD;
	case TERRAIN_BUILDING: case TERRAIN_HALFBUILDING: return THUMB_WALL;
	default: return THUMB_LAND;
	}
}

// Higher-valued features survive reduction of each four-square block.
static void put(uint8_t *p, uint8_t x, uint8_t y, uint8_t v)
{
	if (!p) return;
	p += (y >> 2) * MAP_THUMB_SIZE + (x >> 2);
	if (*p < v) *p = v;
}

int map_thumb(MapSource *s, uint8_t *p)
{
	uint8_t h[12], item[6];
	int i, j, v, x, end, state, remain, half, nib, last;
	if (p) for (i = 0; i < MAP_THUMB_SIZE * MAP_THUMB_SIZE; i++) p[i] = THUMB_SEA;
	if (!bytes(s, h, 12)) return MAP_ERR_SHORT;
	for (i = 0; i < 8; i++) if (h[i] != magic[i]) return MAP_ERR_HEADER;
	if (h[8] != 1) return MAP_ERR_VERSION;
	if (h[9] > MAP_MAX_PILLS || h[10] > MAP_MAX_BASES || h[11] > MAP_MAX_STARTS)
		return MAP_ERR_COUNTS;
	for (i = 0; i < 3; i++) for (j = 0; j < h[9 + i]; j++) {
		if (!bytes(s, item, i == 0 ? 5 : i == 1 ? 6 : 3)) return MAP_ERR_SHORT;
		if (i < 2) put(p, item[0], item[1], i == 0 ? THUMB_PILL : THUMB_BASE);
	}
	for (;;) {
		v = next(s);
		if (v < 0) return MAP_OK;
		h[0] = (uint8_t)v;
		if (!bytes(s, h + 1, 3)) return MAP_ERR_SHORT;
		if (h[0] == 4 && h[1] == 255 && h[2] == 255 && h[3] == 255) return MAP_OK;
		if (h[0] < 4 || h[2] > h[3]) return MAP_ERR_RUN;
		x = h[2]; end = h[3]; state = remain = last = 0;
		for (i = 4; i < h[0]; i++) {
			v = next(s);
			if (v < 0) return MAP_ERR_SHORT;
			for (half = 0; half < 2; half++) {
				nib = half ? v & 15 : v >> 4;
				last = state == 0;
				if (!state) {
					remain = nib < 8 ? nib + 1 : nib - 6;
					state = nib < 8 ? 1 : 2;
				} else {
					j = state == 1 ? 1 : remain;
					if (x + j > end) return MAP_ERR_DATA;
					while (j--) { put(p, (uint8_t)x, h[1], colour((uint8_t)nib)); x++; remain--; }
					if (!remain) state = 0;
				}
			}
		}
		if (x != end || (state && !last)) return MAP_ERR_DATA;
	}
}
