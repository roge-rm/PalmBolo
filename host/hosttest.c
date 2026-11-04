// Host test for the game rules.
//   hosttest               built-in checks, using res/everard.map
//   hosttest FILE_OR_DIR.. also parses every .map file given (or found in
//                          a directory) and prints one line for each
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/game/game.h"
#include "../src/game/mapstore.h"

static int gFails;

#define CHECK(cond, msg) do { if (!(cond)) { printf("FAIL %s\n", msg); gFails++; } } while (0)

static uint8_t *read_file(const char *path, uint32_t *len)
{
	FILE *f = fopen(path, "rb");
	uint8_t *buf;
	long n;

	if (!f)
		return NULL;
	fseek(f, 0, SEEK_END);
	n = ftell(f);
	fseek(f, 0, SEEK_SET);
	buf = malloc(n ? n : 1);
	if (fread(buf, 1, n, f) != (size_t)n) {
		free(buf);
		fclose(f);
		return NULL;
	}
	fclose(f);
	*len = (uint32_t)n;
	return buf;
}

static int parse_buf(MapInfo *info, const uint8_t *data, uint32_t len)
{
	MapSource ms;
	MapMemory mm;

	map_source_memory(&ms, &mm, data, len);
	return map_parse(info, &ms);
}

static uint32_t terrain_sum(void)
{
	uint32_t sum = 0;
	int x, y;

	for (y = 0; y < 256; y++)
		for (x = 0; x < 256; x++)
			sum = sum * 31 + mapstore_get(x, y);
	return sum;
}

static int gParsed, gRejected;

static void parse_path(const char *path)
{
	uint32_t len;
	uint8_t *data = read_file(path, &len);
	MapInfo info;
	int err;

	if (!data) {
		printf("cannot read %s\n", path);
		return;
	}
	err = parse_buf(&info, data, len);
	if (err == MAP_OK) {
		gParsed++;
		printf("ok   %-40s pills %2d bases %2d starts %2d\n", path, info.numPills,
			info.numBases, info.numStarts);
		CHECK(info.numPills <= MAP_MAX_PILLS && info.numBases <= MAP_MAX_BASES &&
			info.numStarts <= MAP_MAX_STARTS, "counts within limits");
	} else {
		gRejected++;
		printf("FAIL %-40s %s\n", path, map_error_text(err));
	}
	free(data);
}

static void parse_dir(const char *dir)
{
	DIR *d = opendir(dir);
	struct dirent *e;
	char path[1024];

	if (!d) {
		parse_path(dir);
		return;
	}
	while ((e = readdir(d))) {
		size_t n = strlen(e->d_name);

		if (n > 4 && strcmp(e->d_name + n - 4, ".map") == 0) {
			snprintf(path, sizeof(path), "%s/%s", dir, e->d_name);
			parse_path(path);
		}
	}
	closedir(d);
}

// Truncated files (a cut between runs is accepted, as in WinBolo) and
// flipped bytes must come back with an error or a
// valid map, never crash. Run under -fsanitize to catch overruns.
static void fuzz(const uint8_t *data, uint32_t len)
{
	MapInfo info;
	uint8_t *copy = malloc(len);
	uint32_t i;
	int rejected = 0, accepted = 0, n;

	for (i = 0; i < len; i += (len / 200) + 1) {
		n = parse_buf(&info, data, i);
		CHECK(n != MAP_OK || i > 206, "truncated file accepted");
		rejected += n != MAP_OK;
	}
	srand(12345);
	for (i = 0; i < 3000; i++) {
		int flips = 1 + rand() % 4, k;

		memcpy(copy, data, len);
		for (k = 0; k < flips; k++)
			copy[rand() % len] ^= (uint8_t)(1 + rand() % 255);
		if (parse_buf(&info, copy, len) == MAP_OK) {
			accepted++;
			CHECK(info.numPills <= MAP_MAX_PILLS && info.numBases <= MAP_MAX_BASES &&
				info.numStarts <= MAP_MAX_STARTS, "fuzz counts");
		} else {
			rejected++;
		}
	}
	free(copy);
	printf("fuzz: %d rejected, %d accepted (flips that left a valid map)\n",
		rejected, accepted);
}

int main(int argc, char **argv)
{
	uint32_t sum = game_selftest();
	GameState a, b;
	int i;
	uint32_t len;
	uint8_t *ev;
	MapInfo info;

	CHECK(mapstore_open(TERRAIN_DEEP_SEA) == 0, "mapstore_open");

	// Same seed and inputs give the same state.
	game_init(&a, 777);
	game_init(&b, 777);
	for (i = 0; i < 500; i++) {
		game_tick(&a, (uint16_t)i);
		game_tick(&b, (uint16_t)i);
	}
	CHECK(game_checksum(&a) == game_checksum(&b), "determinism");
	game_tick(&b, 1);
	CHECK(game_checksum(&a) != game_checksum(&b), "checksum ignores state");
	printf("selftest checksum %08lx\n", (unsigned long)sum);

	// Everard Island, the map built into the app.
	ev = read_file("res/everard.map", &len);
	CHECK(ev != NULL, "read res/everard.map");
	if (ev) {
		CHECK(parse_buf(&info, ev, len) == MAP_OK, "parse Everard Island");
		CHECK(info.numPills == 16 && info.numBases == 11 && info.numStarts == 16,
			"Everard Island counts");
		printf("everard: pills %d bases %d starts %d terrain hash %08lx\n",
			info.numPills, info.numBases, info.numStarts, (unsigned long)terrain_sum());
		CHECK(parse_buf(&info, NULL, 0) == MAP_ERR_SHORT, "empty file");
		ev[0] = 'X';
		CHECK(parse_buf(&info, ev, len) == MAP_ERR_HEADER, "bad header");
		ev[0] = 'B';
		fuzz(ev, len);
		free(ev);
	}

	for (i = 1; i < argc; i++)
		parse_dir(argv[i]);
	if (argc > 1)
		printf("%d parsed, %d rejected\n", gParsed, gRejected);

	if (gFails) {
		printf("%d checks failed\n", gFails);
		return 1;
	}
	printf("ok\n");
	return 0;
}
