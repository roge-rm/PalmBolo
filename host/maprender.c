// Draws a .map file to a PNG with the placeholder tiles.
// Usage: maprender FILE.map OUT.png [TILE_PIXELS [X Y W H]]
// TILE_PIXELS is a multiple of 8 (default 8). X Y W H pick a part of the map
// in tiles (default all 256 x 256).
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/game/map.h"
#include "../src/game/mapstore.h"
#include "../src/game/tiles.h"

static uint32_t crc_table[256];

static void crc_init(void)
{
	uint32_t c, n, k;

	for (n = 0; n < 256; n++) {
		c = n;
		for (k = 0; k < 8; k++)
			c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
		crc_table[n] = c;
	}
}

static uint32_t crc_update(uint32_t crc, const uint8_t *p, size_t n)
{
	while (n--)
		crc = crc_table[(crc ^ *p++) & 255] ^ (crc >> 8);
	return crc;
}

static void put32(uint8_t *p, uint32_t v)
{
	p[0] = v >> 24; p[1] = v >> 16; p[2] = v >> 8; p[3] = v;
}

static void chunk(FILE *f, const char *type, const uint8_t *data, uint32_t len)
{
	uint8_t hdr[8], tail[4];
	uint32_t crc;

	put32(hdr, len);
	memcpy(hdr + 4, type, 4);
	fwrite(hdr, 1, 8, f);
	if (len)
		fwrite(data, 1, len, f);
	crc = crc_update(0xFFFFFFFFu, (const uint8_t *)type, 4);
	crc = crc_update(crc, data, len) ^ 0xFFFFFFFFu;
	put32(tail, crc);
	fwrite(tail, 1, 4, f);
}

// Writes an RGB PNG using stored (uncompressed) deflate blocks.
static void write_png(const char *path, const uint8_t *rgb, int w, int h)
{
	FILE *f = fopen(path, "wb");
	size_t rowlen = (size_t)w * 3 + 1, rawlen = rowlen * h, i, pos = 0;
	uint8_t *raw = malloc(rawlen), *z, ihdr[13];
	uint32_t a = 1, b = 0, zlen;
	int y;

	if (!f) {
		perror(path);
		exit(1);
	}
	for (y = 0; y < h; y++) {
		raw[y * rowlen] = 0;
		memcpy(raw + y * rowlen + 1, rgb + (size_t)y * w * 3, (size_t)w * 3);
	}
	for (i = 0; i < rawlen; i++) {
		a = (a + raw[i]) % 65521;
		b = (b + a) % 65521;
	}
	zlen = 2 + (uint32_t)(rawlen / 65535 + 1) * 5 + (uint32_t)rawlen + 4;
	z = malloc(zlen);
	z[pos++] = 0x78;
	z[pos++] = 0x01;
	for (i = 0; i < rawlen || i == 0;) {
		size_t n = rawlen - i > 65535 ? 65535 : rawlen - i;

		z[pos++] = (i + n >= rawlen) ? 1 : 0;
		z[pos++] = n & 255; z[pos++] = n >> 8;
		z[pos++] = ~n & 255; z[pos++] = (~n >> 8) & 255;
		memcpy(z + pos, raw + i, n);
		pos += n;
		i += n;
		if (n == 0)
			break;
	}
	put32(z + pos, (b << 16) | a);
	pos += 4;
	put32(ihdr, w); put32(ihdr + 4, h);
	ihdr[8] = 8; ihdr[9] = 2; ihdr[10] = ihdr[11] = ihdr[12] = 0;
	fwrite("\x89PNG\r\n\x1a\n", 1, 8, f);
	chunk(f, "IHDR", ihdr, 13);
	chunk(f, "IDAT", z, (uint32_t)pos);
	chunk(f, "IEND", NULL, 0);
	fclose(f);
	free(raw);
	free(z);
}

static uint8_t *gImg;
static int gW, gH, gTs, gX0, gY0;

// Draws tile index t (with a mine mark when mine) at map square (tx, ty).
static void draw_tile(int tx, int ty, int t, int mine)
{
	const TileDef *d = tile_def(t);
	int s = gTs / 8, px, py, i;

	for (py = 0; py < gTs; py++) {
		for (px = 0; px < gTs; px++) {
			int pat = (d->pat[py / s] >> (7 - px / s)) & 1;
			const uint8_t *c = pat ? d->fg : d->bg;
			uint8_t col[3];
			int m = mine ? tile_mine_mark(px / s, py / s) : 0;
			int ox = (tx - gX0) * gTs + px, oy = (ty - gY0) * gTs + py;

			if (m == 1) {
				col[0] = col[1] = col[2] = 255;
			} else if (m == 2) {
				col[0] = 200; col[1] = col[2] = 0;
			} else {
				memcpy(col, c, 3);
			}
			if (ox < 0 || oy < 0 || ox >= gW || oy >= gH)
				continue;
			for (i = 0; i < 3; i++)
				gImg[((size_t)oy * gW + ox) * 3 + i] = col[i];
		}
	}
}

int main(int argc, char **argv)
{
	FILE *f;
	uint8_t *data;
	long len;
	MapInfo info;
	MapSource ms;
	MapMemory mm;
	int x, y, w = 256, h = 256, i, err;

	if (argc < 3) {
		fprintf(stderr, "usage: maprender FILE.map OUT.png [TILE_PIXELS [X Y W H]]\n");
		return 2;
	}
	gTs = argc > 3 ? atoi(argv[3]) : 8;
	if (gTs < 8 || gTs % 8) {
		fprintf(stderr, "tile pixels must be a multiple of 8\n");
		return 2;
	}
	if (argc > 7) {
		gX0 = atoi(argv[4]); gY0 = atoi(argv[5]);
		w = atoi(argv[6]); h = atoi(argv[7]);
	}
	f = fopen(argv[1], "rb");
	if (!f) {
		perror(argv[1]);
		return 1;
	}
	fseek(f, 0, SEEK_END);
	len = ftell(f);
	fseek(f, 0, SEEK_SET);
	data = malloc(len);
	if (fread(data, 1, len, f) != (size_t)len)
		return 1;
	fclose(f);
	mapstore_open(TERRAIN_DEEP_SEA);
	map_source_memory(&ms, &mm, data, (uint32_t)len);
	err = map_parse(&info, &ms);
	if (err) {
		fprintf(stderr, "%s: %s\n", argv[1], map_error_text(err));
		return 1;
	}
	gW = w * gTs;
	gH = h * gTs;
	gImg = malloc((size_t)gW * gH * 3);
	for (y = gY0; y < gY0 + h && y < 256; y++) {
		for (x = gX0; x < gX0 + w && x < 256; x++) {
			uint8_t t = mapstore_get(x, y);
			int mine = t >= TERRAIN_MINE_FIRST && t <= TERRAIN_MINE_LAST;

			draw_tile(x, y, tile_for_terrain(mine ? t - TERRAIN_MINE_ADD : t), mine);
		}
	}
	for (i = 0; i < info.numPills; i++)
		draw_tile(info.pills[i].x, info.pills[i].y, TILE_PILL, 0);
	for (i = 0; i < info.numBases; i++)
		draw_tile(info.bases[i].x, info.bases[i].y, TILE_BASE, 0);
	for (i = 0; i < info.numStarts; i++)
		draw_tile(info.starts[i].x, info.starts[i].y, TILE_START, 0);
	crc_init();
	write_png(argv[2], gImg, gW, gH);
	printf("%s: pills %d bases %d starts %d first start %d,%d\n", argv[1],
		info.numPills, info.numBases, info.numStarts,
		info.numStarts ? info.starts[0].x : -1, info.numStarts ? info.starts[0].y : -1);
	return 0;
}
