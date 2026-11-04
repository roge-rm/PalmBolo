// Host test for the game rules.
//   hosttest               built-in checks, using res/everard.map
//   hosttest FILE_OR_DIR.. also parses every .map file given (or found in
//                          a directory) and prints one line for each
#include <dirent.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/game/game.h"
#include "../src/game/gamepriv.h"
#include "../src/game/mapstore.h"
#include "../src/game/script.h"
#include "../src/game/util.h"

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
		printf("ok   %-40s pills %2d>%2d bases %2d>%2d starts %2d>%2d\n", path,
			data[9], info.numPills, data[10], info.numBases, data[11], info.numStarts);
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

static const char *kTerrainName[16] = {
	"building", "river", "swamp", "crater", "road", "forest", "rubble", "grass",
	"halfbuilding", "boat", "mswamp", "mcrater", "mroad", "mforest", "mrubble", "mgrass"
};

static const char *terrain_name(uint8_t t)
{
	return t < 16 ? kTerrainName[t] : "deepsea";
}

// The base under the tank, or the one it was last on.
static int gLastBase = -1;
static int base_under(const GameState *s)
{
	int j;

	for (j = 0; j < s->map.numBases; j++)
		if (s->map.bases[j].x == s->tanks[0].x >> 8 && s->map.bases[j].y == s->tanks[0].y >> 8)
			gLastBase = j;
	return gLastBase;
}

// Pill shots seen: a new shell with no owner.
static void pills_hit_for_test(GameState *g)
{
	pills_damage(g, 0, R_PILL_SHELL_DAMAGE);
}

// Facts after each script step.
static void fact(const GameState *s, uint8_t n, uint8_t i, void *ctx)
{
	const Tank *k = &s->tanks[0];
	uint8_t mx = k->x >> 8, my = k->y >> 8;

	(void)ctx;
	printf("  %d.%-2d t%-5u sq %3u,%3u+%3u,%3u %-12s spd %2d.%02d ang %6.3f arm %2u sh %2u mi %2u "
		"boat %u dead %u/%u wait %3u shells %u expl %u@%u,%u mexp %u flood %u",
		n, i, (unsigned)s->tick, mx, my, k->x & 255, k->y & 255,
		terrain_name(game_terrain(mx, my)), k->speed / 4, (k->speed % 4) * 25,
		k->angle / 32.0, k->armour, k->shells, k->mines, k->boat, k->destroyed,
		k->deaths, k->deathWait, s->numShells, s->numExpl,
		s->numExpl ? s->expl[0].x : 0, s->numExpl ? s->expl[0].y : 0, s->numMineExp, s->numFlood);
	{
		int b = base_under(s), owned = 0, dead = 0, j;

		for (j = 0; j < s->map.numBases; j++)
			owned += s->map.bases[j].owner == 0;
		for (j = 0; j < s->map.numPills; j++)
			dead += s->map.pills[j].armour == 0;
		printf(" carry %u owned %d deadpills %d", k->numCarry, owned, dead);
		if (b >= 0)
			printf(" base%d own %u %u/%u/%u", b, s->map.bases[b].owner, s->map.bases[b].armour,
				s->map.bases[b].shells, s->map.bases[b].mines);
		printf("\n");
	}
}

static int count_terrain(uint8_t t)
{
	int x, y, n = 0;

	for (y = 0; y < 256; y++)
		for (x = 0; x < 256; x++)
			n += mapstore_get(x, y) == t;
	return n;
}

// Checked facts at the end of the M4 scripts, with WinBolo's numbers.
static void after_script(const GameState *s, uint8_t n)
{
	const Tank *k = &s->tanks[0];
	int b = gLastBase;

	switch (n) {
	case 9:
		CHECK(b >= 0 && s->map.bases[b].owner == 0, "capture: base is the player's");
		break;
	case 10:
		CHECK(k->armour == 40 && k->shells == 40 && k->mines == 40, "refuel: tank full");
		CHECK(b >= 0 && s->map.bases[b].armour < 90 && s->map.bases[b].shells < 90,
			"refuel: base gave stock");
		break;
	case 11:
		CHECK(k->armour < 40 || k->deaths > 0, "pill shot: tank hit");
		break;
	case 12:
		CHECK(k->numCarry == 1, "pill down: carried");
		CHECK(k->numCarry == 1 && s->map.pills[k->carry[0]].inTank &&
			s->map.pills[k->carry[0]].owner == 0, "pill down: owned and in tank");
		break;
	case 13:
		// Restocks at ticks 1000, 2000 and 3000: +1 of each every 1000 ticks.
		CHECK(b >= 0 && s->map.bases[b].armour == 33 && s->map.bases[b].shells == 23 &&
			s->map.bases[b].mines == 13, "restock: +3 each in 3000 ticks");
		break;
	case 14:
		CHECK(k->armour == 40 && k->shells == 32 * 10 / 11 && k->mines == 0 && k->trees == 0,
			"types: tournament shells by neutral bases");
		break;
	}
}

// WinBolo numbers: pill range 2048 (8 squares), a pill fires every 100
// ticks when calm and halves that when hit, a tournament tank's shells.
static void winbolo_numbers(const uint8_t *ev, uint32_t len)
{
	static GameState g;
	GameInput in = { 0, 0 };
	GameSetup st;
	MapPill *p;
	int t, shots = 0, first = -1, second = -1, i;

	CHECK(parse_buf(&g.map, ev, len) == MAP_OK, "numbers map");
	memset(&st, 0, sizeof(st));
	st.numPlayers = 1;
	st.gameType = GAME_STRICT;
	game_init(&g, 7, &st);
	CHECK(g.tanks[0].shells == 0 && g.tanks[0].mines == 0 && g.tanks[0].armour == 40, "strict: armour only");
	st.gameType = GAME_TOURNAMENT;
	game_init(&g, 7, &st);
	CHECK(g.tanks[0].shells == 32 && g.tanks[0].trees == 0, "tournament: 32 shells with all bases neutral");
	st.gameType = GAME_OPEN;
	game_init(&g, 7, &st);
	CHECK(g.tanks[0].shells == 40 && g.tanks[0].mines == 40 && g.tanks[0].trees == 40, "open: full");

	// A pill alone with the tank 2048 units east on open sea: in range.
	for (i = 0; i < g.map.numPills; i++)
		g.map.pills[i].active = 0;
	for (i = 0; i < g.map.numBases; i++)
		g.map.bases[i].active = 0;
	p = &g.map.pills[0];
	p->active = 1;
	p->x = 60;
	p->y = 60;
	p->armour = 15;
	g.tanks[0].boat = BOAT_IN;
	g.tanks[0].x = (60 << 8) + 128 + 2048;
	g.tanks[0].y = (60 << 8) + 128;
	g.tanks[0].speed = 0;
	for (t = 0; t < 400; t++) {
		uint8_t before = g.numShells;

		g.tanks[0].x = (60 << 8) + 128 + 2048;
		g.tanks[0].y = (60 << 8) + 128;
		g.tanks[0].armour = 40;
		g.tanks[0].boat = BOAT_IN;
		g.tanks[0].destroyed = 0;
		g.tanks[0].deathWait = 0;
		game_tick(&g, &in);
		if (g.numShells > before && g.shells[0].owner == GAME_NO_PLAYER) {
			shots++;
			if (first < 0)
				first = t;
			else if (second < 0)
				second = t;
		}
	}
	printf("pill at 2048: %d shots, first at tick %d, then every %d\n", shots, first, second - first);
	CHECK(shots >= 3 && second - first == 100, "pill fires every 100 ticks at 2048");
	// One further: out of range.
	g.numShells = 0;
	p->justSeen = 0;
	shots = 0;
	for (t = 0; t < 300; t++) {
		uint8_t before = g.numShells;

		g.tanks[0].x = (60 << 8) + 128 + 2049;
		g.tanks[0].y = (60 << 8) + 128;
		g.tanks[0].boat = BOAT_IN;
		game_tick(&g, &in);
		shots += g.numShells > before;
	}
	CHECK(shots == 0, "pill ignores a tank at 2049");
	// Angry: one hit halves the reload.
	pills_hit_for_test(&g);
	printf("pill hit: speed %u cooldown %u armour %u\n", p->speed, p->coolDown, p->armour);
	CHECK(p->speed == 50 && p->coolDown == 32 && p->armour == 14, "pill angry after a hit");
	// Owned by the player: no shots.
	p->owner = 0;
	p->speed = 100;
	g.numShells = 0;
	shots = 0;
	for (t = 0; t < 300; t++) {
		uint8_t before = g.numShells;

		g.tanks[0].x = (60 << 8) + 128 + 512;
		g.tanks[0].y = (60 << 8) + 128;
		g.tanks[0].boat = BOAT_IN;
		game_tick(&g, &in);
		shots += g.numShells > before;
	}
	CHECK(shots == 0, "own pill holds fire");
	// util_angle against WinBolo's float utilCalcAngle.
	{
		static const uint16_t pts[][4] = {
			{ 1000, 1000, 1000, 500 }, { 1000, 1000, 1500, 1000 }, { 1000, 1000, 1000, 1500 },
			{ 1000, 1000, 500, 1000 }, { 1000, 1000, 1300, 700 }, { 1000, 1000, 1700, 1250 },
			{ 1000, 1000, 400, 1900 }, { 1000, 1000, 990, 200 }
		};
		for (i = 0; i < 8; i++) {
			double gx = abs(pts[i][2] - pts[i][0]), gy = abs(pts[i][3] - pts[i][1]);
			double a = atan(gx / gy) / 6.28318 * 360.0 * (256.0 / 360.0);
			int dx = pts[i][2] - pts[i][0], dy = pts[i][3] - pts[i][1];
			double want;
			uint16_t got = util_angle(pts[i][0], pts[i][1], pts[i][2], pts[i][3]);

			if (dx <= 0 && dy <= 0)
				want = 256 - a;
			else if (dx > 0 && dy >= 0)
				want = 128 - a;
			else if (dx <= 0 && dy >= 0)
				want = a + 128;
			else
				want = a;
			printf("angle %d: winbolo %.3f here %.3f\n", i, want, got / 32.0);
			CHECK(fabs(want - got / 32.0) <= 1.0 / 32, "util_angle within 1/32 bradian");
		}
	}
}

static void scripts(const uint8_t *ev, uint32_t len)
{
	static GameState g;
	uint8_t n;

	printf("sizeof(GameState) %u, Tank %u, Shell %u, MapInfo %u\n", (unsigned)sizeof(GameState),
		(unsigned)sizeof(Tank), (unsigned)sizeof(Shell), (unsigned)sizeof(MapInfo));
	for (n = 0; n < SCRIPT_COUNT; n++) {
		uint32_t c;
		int craters, rubble, half, boats, mines;

		CHECK(parse_buf(&g.map, ev, len) == MAP_OK, "script map");
		printf("script %d %s\n", n, script_name(n));
		gLastBase = -1;
		c = script_run(&g, n, fact, NULL);
		after_script(&g, n);
		craters = count_terrain(TERRAIN_CRATER);
		rubble = count_terrain(TERRAIN_RUBBLE);
		half = count_terrain(TERRAIN_HALFBUILDING);
		boats = count_terrain(TERRAIN_BOAT);
		mines = 0;
		for (int t = TERRAIN_MINE_FIRST; t <= TERRAIN_MINE_LAST; t++)
			mines += count_terrain(t);
		printf("script %d %-8s checksum %08lx  craters %d rubble %d halfbuildings %d boats %d mines %d\n",
			n, script_name(n), (unsigned long)c, craters, rubble, half, boats, mines);
		// Same script twice gives the same answer.
		CHECK(parse_buf(&g.map, ev, len) == MAP_OK, "script map");
		CHECK(script_run(&g, n, NULL, NULL) == c, "script determinism");
	}
}

int main(int argc, char **argv)
{
	int i;
	uint32_t len;
	uint8_t *ev;
	MapInfo info;

	CHECK(mapstore_open(TERRAIN_DEEP_SEA) == 0, "mapstore_open");

	printf("table check %08lx (UTIL_TABLE_CHECK %08lx)\n",
		(unsigned long)util_table_check(), (unsigned long)UTIL_TABLE_CHECK);
	CHECK(util_table_check() == UTIL_TABLE_CHECK, "table check");

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
		scripts(ev, len);
		winbolo_numbers(ev, len);
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
