// Focused checks for menu geometry and the read-only map overview decoder.
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/game/mapthumb.h"
#include "../src/frontlayout.h"

typedef struct Input { const uint8_t *p; size_t n, at, chunk; } Input;
static uint32_t fill(void *ctx, uint8_t *out, uint32_t n)
{
	Input *in = ctx;
	if (n > in->n - in->at) n = in->n - in->at;
	if (n > in->chunk) n = in->chunk;
	memcpy(out, in->p + in->at, n); in->at += n;
	return n;
}
static int decode(const uint8_t *p, size_t n, size_t chunk, uint8_t *pixels)
{
	Input in = {p, n, 0, chunk};
	MapSource s;
	memset(&s, 0, sizeof(s)); s.ctx = &in; s.fill = fill;
	return map_thumb(&s, pixels);
}

static void geometry(void)
{
	int h, i, y, next, size, list;
	const int setup[] = {27,44,61,78,95,112};
	const int settings[] = {26,43,57,71,101,120};
	for (h = 160; h <= 240; h++) {
		for (i = 0; i < 6; i++) {
			y = setup[i] + front_shift(NewGameForm, setup[i], h);
			assert(y >= 22 && y + 12 <= h - 23);
			if (i < 5) {
				next = setup[i+1] + front_shift(NewGameForm, setup[i+1], h);
				assert(y + 12 <= next);
			}
			y = settings[i] + front_shift(SettingsForm, settings[i], h);
			assert(y >= 22 && y + 13 <= h - 23);
		}
		assert(front_lobby_row(h) >= 11);
		assert(front_lobby_y(h) + 8 * front_lobby_row(h) <= h - 34);
		assert(front_results_row(h) >= 11);
		assert(front_results_y(h) + 8 * front_results_row(h) <= h - 23);
		size = front_map_size(h); list = front_map_list_y(h);
		assert(26 + size + 1 < list);
		assert((h - 28 - list) / 11 >= 3);
		if (h >= 210) {
			y = 116 + (h - 210) / 2 + 3 * 21;
			assert(y + 14 < h - 12);
		}
	}
}

static void resource_geometry(void)
{
	FILE *f = fopen("src/palmbolo.rcp", "r");
	char line[256], name[40], *at;
	int form = 0, x, y, w, h, n, height, moved, labelY = -1;
	assert(f);
	while (fgets(line, sizeof(line), f)) {
		if (sscanf(line, "FORM ID %39s", name) == 1) {
			form = !strcmp(name,"NewGameForm") ? NewGameForm :
				!strcmp(name,"SettingsForm") ? SettingsForm :
				!strcmp(name,"LobbyForm") ? LobbyForm :
				!strcmp(name,"JoinForm") ? JoinForm :
				!strcmp(name,"ResultsForm") ? ResultsForm : 0;
			labelY = -1;
			continue;
		}
		if (!form || strstr(line,"LIST ") || strstr(line,"GRAFFITI")) continue;
		at = strstr(line,"AT (");
		if (!at) continue;
		n = sscanf(at,"AT (%d %d %d %d",&x,&y,&w,&h);
		assert(n == 2 || n == 4);
		if (n == 2) { w = 1; h = 11; }
		assert(x >= 0 && x+w <= 160);
		if (form == NewGameForm && strstr(line,"LABEL ")) labelY = y;
		if (form == NewGameForm && strstr(line,"POPUPTRIGGER ")) assert(labelY == y);
		for (height = 160; height <= 240; height++) {
			moved = y + (y == 143 ? height-160 : front_shift(form,y,height));
			assert(moved >= 22 && moved+h <= height);
			if (y != 143) assert(moved+h <= height-23);
		}
	}
	fclose(f);
}

int main(void)
{
	uint8_t pixels[4096], other[4096], bad[64];
	/* A pill and base on the same reduced cells as two grass runs. */
	const uint8_t sample[] = {
		'B','M','A','P','B','O','L','O',1,1,1,0,
		8,16,255,15,50, 12,16,255,90,90,90,
		5,16,8,16,0xe7, 5,20,8,16,0xe5,
		4,255,255,255
	};
	const uint8_t different[] = {
		'B','M','A','P','B','O','L','O',1,0,0,0,
		7,0,0,4,0x34,0x17,0x50, 4,255,255,255
	};
	size_t i, n;
	FILE *f;
	uint8_t *file;
	unsigned seen[THUMB_COLOURS] = {0};

	geometry();
	resource_geometry();
	assert(decode(sample, sizeof(sample), 256, pixels) == MAP_OK);
	assert(pixels[4*64+2] == THUMB_PILL);
	assert(pixels[4*64+3] == THUMB_BASE);
	assert(pixels[5*64+2] == THUMB_FOREST);
	assert(pixels[5*64+3] == THUMB_FOREST);
	assert(pixels[0] == THUMB_SEA && pixels[4095] == THUMB_SEA);
	assert(decode(sample, sizeof(sample), 1, other) == MAP_OK);
	assert(memcmp(pixels, other, sizeof(pixels)) == 0);
	assert(decode(different, sizeof(different), 2, pixels) == MAP_OK);
	assert(pixels[0] == THUMB_ROAD);
	for (i = 0; i < 23; i++) assert(decode(sample, i, 3, pixels) != MAP_OK);
	memcpy(bad, sample, sizeof(sample)); bad[0] = 'X';
	assert(decode(bad, sizeof(sample), 1, pixels) == MAP_ERR_HEADER);
	memcpy(bad, sample, sizeof(sample)); bad[8] = 2;
	assert(decode(bad, sizeof(sample), 1, pixels) == MAP_ERR_VERSION);
	memcpy(bad, sample, sizeof(sample)); bad[9] = 255;
	assert(decode(bad, sizeof(sample), 1, pixels) == MAP_ERR_COUNTS);
	memcpy(bad, sample, sizeof(sample)); bad[23] = 3;
	assert(decode(bad, sizeof(sample), 1, pixels) == MAP_ERR_RUN);
	memcpy(bad, sample, sizeof(sample)); bad[26] = 9;
	assert(decode(bad, sizeof(sample), 1, pixels) == MAP_ERR_DATA);
	f = fopen("res/everard.map", "rb"); assert(f);
	fseek(f, 0, SEEK_END); n = (size_t)ftell(f); rewind(f);
	file = malloc(n); assert(file); assert(fread(file, 1, n, f) == n); fclose(f);
	assert(decode(file, n, 256, pixels) == MAP_OK);
	assert(decode(file, n, 7, other) == MAP_OK);
	assert(memcmp(pixels, other, sizeof(pixels)) == 0);
	for (i = 0; i < sizeof(pixels); i++) { assert(pixels[i] < THUMB_COLOURS); seen[pixels[i]]++; }
	assert(seen[THUMB_SEA] && seen[THUMB_LAND] && seen[THUMB_FOREST]);
	assert(seen[THUMB_BASE] && seen[THUMB_PILL]);
	free(file);
	puts("Menu geometry (160..240), map decoding, malformed maps and chunked reads passed.");
	return 0;
}
