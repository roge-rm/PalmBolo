// Draws a .map file to a PNG with the colour art, as the game shows it
// before anyone owns anything: pills and bases neutral, no mines shown.
// Usage: maprender FILE.map OUT.png [TILE_PIXELS [X Y W H]]
// TILE_PIXELS is 8 or 16 (default 8). X Y W H pick a part of the map in
// tiles (default all 256 x 256). The art is read from ART_DIR (build/art),
// made by tools/art2palm.py.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/game/map.h"
#include "../src/game/mapstore.h"
#include "tilepick.h"
#include "art.h"

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

static uint8_t *gImg, *gArt;
static int gW, gH, gTs, gX0, gY0, gArtW;

// Loads ART_DIR/terrainN.rgb: width and height (16-bit little-endian), RGB.
static void load_art(void)
{
	char path[256];
	uint8_t hdr[4];
	FILE *f;
	size_t n;

	snprintf(path, sizeof path, "%s/terrain%d.rgb", ART_DIR, gTs);
	f = fopen(path, "rb");
	if (!f || fread(hdr, 1, 4, f) != 4) {
		perror(path);
		exit(1);
	}
	gArtW = hdr[0] | hdr[1] << 8;
	n = (size_t)gArtW * (hdr[2] | hdr[3] << 8) * 3;
	gArt = malloc(n);
	if (fread(gArt, 1, n, f) != n) {
		fprintf(stderr, "%s: short file\n", path);
		exit(1);
	}
	fclose(f);
}

// Draws art cell c at map square (tx, ty).
static void draw_tile(int tx, int ty, int c)
{
	int px, py, sx = (c % ART_COLS) * gTs, sy = (c / ART_COLS) * gTs;

	for (py = 0; py < gTs; py++) {
		for (px = 0; px < gTs; px++) {
			int ox = (tx - gX0) * gTs + px, oy = (ty - gY0) * gTs + py;

			if (ox < 0 || oy < 0 || ox >= gW || oy >= gH)
				continue;
			memcpy(gImg + ((size_t)oy * gW + ox) * 3,
				gArt + ((size_t)(sy + py) * gArtW + sx + px) * 3, 3);
		}
	}
}

static int is_base(const MapInfo *m, int x, int y)
{
	int i;

	for (i = 0; i < m->numBases; i++)
		if (m->bases[i].x == (x & 255) && m->bases[i].y == (y & 255))
			return 1;
	return 0;
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
	if (gTs != 8 && gTs != 16) {
		fprintf(stderr, "tile pixels must be 8 or 16\n");
		return 2;
	}
	load_art();
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
		const uint8_t *up = mapstore_row((uint8_t)(y - 1)), *row = mapstore_row((uint8_t)y);
		const uint8_t *down = mapstore_row((uint8_t)(y + 1));

		for (x = gX0; x < gX0 + w && x < 256; x++) {
			uint8_t t = row[x], n[8];

			if (t >= TERRAIN_MINE_FIRST && t <= TERRAIN_MINE_LAST)
				t = (uint8_t)(t - TERRAIN_MINE_ADD);
			tile_neighbours(up, row, down, (uint8_t)x, n);
			if (tile_bases_matter(t)) {
				int k, dx[8] = {-1, 0, 1, -1, 1, -1, 0, 1}, dy[8] = {-1, -1, -1, 0, 0, 1, 1, 1};

				for (k = 0; k < 8; k++)
					if (is_base(&info, x + dx[k], y + dy[k]))
						n[k] = TERRAIN_ROAD;
			}
			draw_tile(x, y, tile_pick(t, n));
		}
	}
	for (i = 0; i < info.numStarts; i++)
		draw_tile(info.starts[i].x, info.starts[i].y, AT_START);
	for (i = 0; i < info.numBases; i++)
		draw_tile(info.bases[i].x, info.bases[i].y, AT_BASE_NEUTRAL);
	for (i = 0; i < info.numPills; i++)
		draw_tile(info.pills[i].x, info.pills[i].y, AT_PILL_ENEMY_0);
	crc_init();
	write_png(argv[2], gImg, gW, gH);
	printf("%s: pills %d bases %d starts %d first start %d,%d\n", argv[1],
		info.numPills, info.numBases, info.numStarts,
		info.numStarts ? info.starts[0].x : -1, info.numStarts ? info.starts[0].y : -1);
	return 0;
}
