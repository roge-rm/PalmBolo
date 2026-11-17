// Map file writer, the reverse of map.c's parser. Run coding as WinBolo 2
// writes it (src/bolo/bolo_map.c), Copyright (c) 1998-2026 John Morrison,
// GPLv3. This version Copyright (c) 2026 the PalmBolo authors, GPLv3.
//
// Each row is cut into runs at deep sea squares, as deep sea has no nibble.
// Within a run, 2 to 9 identical squares go as one item, other squares are
// gathered into items of up to 8 different squares. An odd nibble count is
// padded with a 0.

#include "map.h"
#include "mapstore.h"

typedef struct Writer {
	MapWriteProc write;
	void *ctx;
	int err;
} Writer;

static void SEC_COLD put(Writer *w, const uint8_t *src, uint32_t len)
{
	if (w->err == MAP_OK && w->write(w->ctx, src, len) != 0)
		w->err = MAP_ERR_WRITE;
}

// Codes squares sx..ex-1 of row y as one run and writes it.
static void SEC_COLD put_run(Writer *w, const uint8_t *rw, uint8_t y, uint8_t sx, uint8_t ex)
{
	// 4 header bytes, then the nibbles. The worst row is a single square
	// then a pair, over and over: 4 nibbles for 3 squares, 170 bytes for 255.
	uint8_t buf[4 + 172];
	uint16_t nib = 0, x = sx, n, k;

#define NIB(v) do { \
	if (nib & 1) buf[4 + (nib >> 1)] |= (uint8_t)(v); \
	else buf[4 + (nib >> 1)] = (uint8_t)((v) << 4); \
	nib++; \
} while (0)

	while (x < ex) {
		for (n = 1; x + n < ex && rw[x + n] == rw[x] && n < 9; n++)
			;
		if (n >= 2) {
			NIB(n + 6);
			NIB(rw[x]);
			x += n;
			continue;
		}
		// Different squares, up to 8, stopping before two the same.
		for (k = 1; k < 8 && x + k < ex; k++)
			if (x + k + 1 < ex && rw[x + k + 1] == rw[x + k])
				break;
		NIB(k - 1);
		for (n = 0; n < k; n++)
			NIB(rw[x + n]);
		x += k;
	}
	if (nib & 1)
		NIB(0);
#undef NIB
	buf[0] = (uint8_t)(4 + (nib >> 1));
	buf[1] = y;
	buf[2] = sx;
	buf[3] = ex;
	put(w, buf, buf[0]);
}

static const uint8_t kHead[9] = { 'B', 'M', 'A', 'P', 'B', 'O', 'L', 'O', 1 };
static const uint8_t kEnd[4] = { 4, 255, 255, 255 };

int SEC_COLD map_write(const MapInfo *info, MapWriteProc write, void *ctx)
{
	Writer w;
	uint8_t b[6];
	uint16_t i, x, y, sx;
	const uint8_t *rw;

	if (info->numPills > MAP_MAX_PILLS || info->numBases > MAP_MAX_BASES ||
	    info->numStarts > MAP_MAX_STARTS)
		return MAP_ERR_COUNTS;
	// A run ends one past its last square in a byte, so x 255 can't be land.
	for (y = 0; y < 256; y++)
		if (mapstore_get(255, y) != TERRAIN_DEEP_SEA)
			return MAP_ERR_EDGE;

	w.write = write;
	w.ctx = ctx;
	w.err = MAP_OK;
	put(&w, kHead, sizeof(kHead));
	b[0] = info->numPills;
	b[1] = info->numBases;
	b[2] = info->numStarts;
	put(&w, b, 3);
	for (i = 0; i < info->numPills; i++) {
		const MapPill *p = &info->pills[i];
		b[0] = p->x; b[1] = p->y; b[2] = p->owner; b[3] = p->armour; b[4] = p->speed;
		put(&w, b, 5);
	}
	for (i = 0; i < info->numBases; i++) {
		const MapBase *p = &info->bases[i];
		b[0] = p->x; b[1] = p->y; b[2] = p->owner; b[3] = p->armour;
		b[4] = p->shells; b[5] = p->mines;
		put(&w, b, 6);
	}
	for (i = 0; i < info->numStarts; i++) {
		const MapStart *p = &info->starts[i];
		b[0] = p->x; b[1] = p->y; b[2] = p->dir;
		put(&w, b, 3);
	}

	for (y = 0; y < 256 && w.err == MAP_OK; y++) {
		rw = mapstore_row(y);
		for (x = 0; x < 255; ) {
			if (rw[x] == TERRAIN_DEEP_SEA) {
				x++;
				continue;
			}
			for (sx = x; x < 255 && rw[x] != TERRAIN_DEEP_SEA; x++)
				;
			put_run(&w, rw, (uint8_t)y, (uint8_t)sx, (uint8_t)x);
		}
	}
	put(&w, kEnd, sizeof(kEnd));
	return w.err;
}
