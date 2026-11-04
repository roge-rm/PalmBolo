// Map file parser. The format and the run decoding follow WinBolo 2
// (src/bolo/bolo_map.c), Copyright (c) 1998-2026 John Morrison, GPLv3.
// This version Copyright (c) 2026 the PalmBolo authors, GPLv3.
//
// File layout: "BMAPBOLO", version 1, pill, base and start counts, then 5
// bytes per pill (x y owner armour speed), 6 per base (x y owner armour
// shells mines), 3 per start (x y dir). Then runs, each with a header of
// datalen (including the header), y, startx, endx (one past the last
// square), followed by nibbles. A nibble below 8 is a length - 1 of
// different squares, one nibble each. 8 to 15 is a length + 6 of identical
// squares, and the nibble after it is the terrain. The runs end with a
// header of 4, 255, 255, 255.

#include "map.h"
#include "mapstore.h"

static uint32_t memory_fill(void *ctx, uint8_t *dst, uint32_t max)
{
	MapMemory *mm = (MapMemory *)ctx;
	uint32_t n = mm->len - mm->pos;
	uint32_t i;

	if (n > max)
		n = max;
	for (i = 0; i < n; i++)
		dst[i] = mm->data[mm->pos + i];
	mm->pos += n;
	return n;
}

void map_source_memory(MapSource *ms, MapMemory *mm, const uint8_t *data, uint32_t len)
{
	mm->data = data;
	mm->len = len;
	mm->pos = 0;
	ms->fill = memory_fill;
	ms->ctx = mm;
	ms->pos = ms->n = 0;
	ms->eof = 0;
}

// Next byte, or -1 at the end of the file.
static int get_byte(MapSource *ms)
{
	if (ms->pos >= ms->n) {
		if (ms->eof)
			return -1;
		ms->n = (uint16_t)ms->fill(ms->ctx, ms->buf, sizeof(ms->buf));
		ms->pos = 0;
		if (ms->n == 0) {
			ms->eof = 1;
			return -1;
		}
	}
	return ms->buf[ms->pos++];
}

static int get_bytes(MapSource *ms, uint8_t *dst, int n)
{
	int i, c;

	for (i = 0; i < n; i++) {
		c = get_byte(ms);
		if (c < 0)
			return 0;
		dst[i] = (uint8_t)c;
	}
	return 1;
}

// Scratch row for read_run, kept out of the stack on the Palm.
static uint8_t row[MAP_SIZE];

// Decodes the data bytes of one run into row y, squares startx..endx-1.
static int read_run(MapSource *ms, int elems, uint8_t y, int startx, int endx)
{
	enum { LEN, DIFF, SAME };
	int state = LEN, remain = 0, x = startx, i, c, half, lenLast = 0;
	uint8_t nib;

	for (i = 0; i < elems; i++) {
		c = get_byte(ms);
		if (c < 0)
			return MAP_ERR_SHORT;
		for (half = 0; half < 2; half++) {
			nib = (uint8_t)(half == 0 ? (c >> 4) : (c & 15));
			lenLast = state == LEN;
			if (state == LEN) {
				if (nib < 8) {
					remain = nib + 1;
					state = DIFF;
				} else {
					remain = nib - 6;
					state = SAME;
				}
			} else if (state == DIFF) {
				if (x >= endx)
					return MAP_ERR_DATA;
				row[x] = nib;
				x++;
				if (--remain == 0)
					state = LEN;
			} else {
				if (x + remain > endx)
					return MAP_ERR_DATA;
				while (remain--)
					row[x++] = nib;
				state = LEN;
			}
		}
	}
	// A length nibble at the very end is padding to a whole byte. Anything
	// else left over means the run stopped in the middle of an item.
	if (x != endx || (state != LEN && !lenLast))
		return MAP_ERR_DATA;
	if (endx > startx)
		mapstore_set_bytes((uint8_t)startx, y, (uint16_t)(endx - startx), row + startx);
	return MAP_OK;
}

static int read_runs(MapSource *ms)
{
	uint8_t h[4];
	int err, c;

	for (;;) {
		// Some maps end without the closing header, so a file that ends
		// exactly between runs is accepted.
		c = get_byte(ms);
		if (c < 0)
			return MAP_OK;
		h[0] = (uint8_t)c;
		if (!get_bytes(ms, h + 1, 3))
			return MAP_ERR_SHORT;
		if (h[0] == 4 && h[1] == 255 && h[2] == 255 && h[3] == 255)
			return MAP_OK;
		if (h[0] < 4 || h[2] > h[3])
			return MAP_ERR_RUN;
		err = read_run(ms, h[0] - 4, h[1], h[2], h[3]);
		if (err != MAP_OK)
			return err;
	}
}

static int parse(MapInfo *info, MapSource *ms)
{
	static const char magic[8] = { 'B', 'M', 'A', 'P', 'B', 'O', 'L', 'O' };
	uint8_t b[8];
	int i, err;

	if (!get_bytes(ms, b, 8))
		return MAP_ERR_SHORT;
	for (i = 0; i < 8; i++)
		if (b[i] != (uint8_t)magic[i])
			return MAP_ERR_HEADER;
	if (!get_bytes(ms, b, 4))
		return MAP_ERR_SHORT;
	if (b[0] != 1)
		return MAP_ERR_VERSION;
	if (b[1] > MAP_MAX_PILLS || b[2] > MAP_MAX_BASES || b[3] > MAP_MAX_STARTS)
		return MAP_ERR_COUNTS;
	info->numPills = b[1];
	info->numBases = b[2];
	info->numStarts = b[3];

	for (i = 0; i < info->numPills; i++) {
		MapPill *p = &info->pills[i];

		if (!get_bytes(ms, b, 5))
			return MAP_ERR_SHORT;
		p->x = b[0];
		p->y = b[1];
		p->owner = (b[2] > 15 && b[2] != MAP_NEUTRAL) ? MAP_NEUTRAL : b[2];
		p->armour = b[3];
		p->speed = b[4];
	}
	for (i = 0; i < info->numBases; i++) {
		MapBase *p = &info->bases[i];

		if (!get_bytes(ms, b, 6))
			return MAP_ERR_SHORT;
		p->x = b[0];
		p->y = b[1];
		p->owner = (b[2] > 15 && b[2] != MAP_NEUTRAL) ? MAP_NEUTRAL : b[2];
		p->armour = b[3];
		p->shells = b[4];
		p->mines = b[5];
	}
	for (i = 0; i < info->numStarts; i++) {
		MapStart *p = &info->starts[i];

		if (!get_bytes(ms, b, 3))
			return MAP_ERR_SHORT;
		p->x = b[0];
		p->y = b[1];
		p->dir = b[2] & 15;
	}

	err = read_runs(ms);
	if (err != MAP_OK)
		return err;

	// Bases stand on road. A pill can't be in water or a building, and has
	// no mine under it.
	for (i = 0; i < info->numBases; i++)
		mapstore_set(info->bases[i].x, info->bases[i].y, TERRAIN_ROAD);
	for (i = 0; i < info->numPills; i++) {
		uint8_t t = mapstore_get(info->pills[i].x, info->pills[i].y);

		if (t == TERRAIN_RIVER || t == TERRAIN_DEEP_SEA ||
		    t == TERRAIN_BUILDING || t == TERRAIN_HALFBUILDING)
			mapstore_set(info->pills[i].x, info->pills[i].y, TERRAIN_ROAD);
		else if (t >= TERRAIN_MINE_FIRST && t <= TERRAIN_MINE_LAST)
			mapstore_set(info->pills[i].x, info->pills[i].y,
				(uint8_t)(t - TERRAIN_MINE_ADD));
	}
	return MAP_OK;
}

int map_parse(MapInfo *info, MapSource *ms)
{
	int err;
	uint8_t *p = (uint8_t *)info;
	uint16_t i;

	for (i = 0; i < sizeof(*info); i++)
		p[i] = 0;
	mapstore_fill(TERRAIN_DEEP_SEA);
	err = parse(info, ms);
	if (err != MAP_OK)
		for (i = 0; i < sizeof(*info); i++)
			p[i] = 0;
	return err;
}

const char *map_error_text(int err)
{
	switch (err) {
	case MAP_OK: return "ok";
	case MAP_ERR_SHORT: return "file too short";
	case MAP_ERR_HEADER: return "not a map file";
	case MAP_ERR_VERSION: return "unknown map version";
	case MAP_ERR_COUNTS: return "too many pills, bases or starts";
	case MAP_ERR_RUN: return "bad run header";
	case MAP_ERR_DATA: return "bad run data";
	}
	return "unknown error";
}
