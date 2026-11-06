// Host test for the game rules.
//   hosttest               built-in checks, using res/everard.map
//   hosttest --lockstep [SEEDS [TICKS]]  lockstep games over the loopback
//   hosttest FILE_OR_DIR.. also parses every .map file given (or found in
//                          a directory) and prints one line for each
#include <dirent.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "../src/game/game.h"
#include "../src/game/gamepriv.h"
#include "../src/game/mapstore.h"
#include "../src/game/script.h"
#include "../src/game/util.h"

static int gFails;

int lockstep_tests(const uint8_t *ev, uint32_t len, int seeds, uint32_t ticks);

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

static int count_forest(void)
{
	int x, y, n = 0;

	for (y = 0; y < 256; y++)
		for (x = 0; x < 256; x++)
			n += mapstore_get(x, y) == TERRAIN_FOREST || mapstore_get(x, y) == TERRAIN_MINE_FOREST;
	return n;
}

static int gForests[2], gManDied;
// Tank 0 to tank 1 (dx + dy) in the collide script, after driving into
// each other and at the end.
static int gDriveGap, gLastGap;

// Facts after each script step.
// Terrain one and two squares east of the tank after each step of the
// boat wreck script.
static uint8_t gWreck[9][2];
// Sound events seen in a script, by kind (at most GAME_MAX_SOUNDS a step).
static int gSounds[SND_KINDS];

static void fact(const GameState *s, uint8_t n, uint8_t i, void *ctx)
{
	const Tank *k = &s->tanks[0];
	uint8_t mx = k->x >> 8, my = k->y >> 8;

	(void)ctx;
	{
		GameSound snd[GAME_MAX_SOUNDS];
		uint8_t j, c = game_take_sounds(snd, GAME_MAX_SOUNDS);

		for (j = 0; j < c; j++)
			gSounds[snd[j].kind]++;
	}
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
		if (n >= 15) {
			const Builder *m = &s->men[0];
			uint8_t tx = m->destX >> 8, ty = m->destY >> 8;
			int pi = g_pill_at(s, tx, ty);

			printf("\n        man st %u at %u,%u+%u,%u in %u dead %u act %u carries %u/%u/%u next %u"
				" | trees %u | target %u,%u %s",
				m->state, m->x >> 8, m->y >> 8, m->x & 255, m->y & 255, m->inTank, m->isDead,
				m->action, m->numTrees, m->numMines, m->pill, m->nextAction, k->trees, tx, ty,
				terrain_name(game_terrain(tx, ty)));
			if (pi >= 0)
				printf(" pill%d arm %u own %u", pi, s->map.pills[pi].armour, s->map.pills[pi].owner);
			if (n == 23) {
				int f = count_forest();

				printf(" | forests %d grow %u,%u score %d time %d", f,
					s->treeX, s->treeY, s->treeScore, s->treeTime);
				gForests[i == 0 ? 0 : 1] = f;
			}
			if (m->isDead)
				gManDied = 1;
		}
		if (n >= 28) {
			int j;

			for (j = 1; j < s->numTanks; j++) {
				const Tank *o = &s->tanks[j];

				printf("\n        tank%d sq %3u,%3u+%3u,%3u spd %2d arm %2u dead %u/%u wait %3u kills %u"
					" obst %u | tank0 kills %u obst %u",
					j, o->x >> 8, o->y >> 8, o->x & 255, o->y & 255, o->speed / 4, o->armour,
					o->destroyed, o->deaths, o->deathWait, o->kills, o->obstructed,
					k->kills, k->obstructed);
			}
			if (n == 28) {
				int dx = abs((int)s->tanks[1].x - k->x), dy = abs((int)s->tanks[1].y - k->y);

				if (i == 11)
					gDriveGap = s->tanks[1].x > k->x ? dx + dy : -1;
				gLastGap = dx + dy;
			}
		}
		if (n == 25 && i < 9) {
			gWreck[i][0] = game_terrain(mx + 1, my);
			gWreck[i][1] = game_terrain(mx + 2, my);
			printf("\n        wreck east 1 %s, 2 %s", terrain_name(gWreck[i][0]),
				terrain_name(gWreck[i][1]));
		}
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
	const Builder *m = &s->men[0];
	uint8_t mx = m->destX >> 8, my = m->destY >> 8;
	int pi = g_pill_at(s, mx, my);
	int b = gLastBase;

	switch (n) {
	case 25:
		CHECK(gWreck[2][1] == TERRAIN_BOAT && gWreck[3][1] == TERRAIN_CRATER,
			"boat wreck: small wreck craters the boat");
		CHECK(gWreck[4][1] == TERRAIN_RIVER, "boat wreck: crater floods");
		CHECK(gWreck[7][0] == TERRAIN_CRATER && gWreck[7][1] == TERRAIN_BOAT,
			"boat wreck: big wreck spares the boat");
		break;
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
	// The builder, with WinBolo's numbers: 4 trees from a forest, a road or
	// wall costs 2, a wall repair 1, a boat 20, a new pill 4, a pill repair
	// up to 4 with what isn't needed given back, a mine 1.
	case 15:
		CHECK(k->trees == 4 && game_terrain(mx, my) == TERRAIN_GRASS && m->inTank,
			"trees: 4 gathered, forest to grass");
		break;
	case 16:
		CHECK(k->trees == 8 && game_terrain(mx, my) == TERRAIN_ROAD && m->inTank,
			"road: costs 2, a second refused");
		break;
	case 17:
		CHECK(k->trees == 7 && game_terrain(mx, my) == TERRAIN_BUILDING && m->inTank,
			"wall: costs 2, repair 1, own square refused");
		break;
	case 18:
		CHECK(k->trees == 10 && game_terrain(mx, my) == TERRAIN_BOAT && m->inTank,
			"boat: a wall on a river costs 20");
		break;
	case 19:
		CHECK(k->trees == 6 && k->numCarry == 0 && pi >= 0 && s->map.pills[pi].armour == 15 &&
			s->map.pills[pi].owner == 0 && !s->map.pills[pi].inTank, "put pill: costs 4, full armour");
		break;
	case 20:
		// Armour 5 needs 3 trees of the 4 taken.
		CHECK(k->trees == 7 && pi >= 0 && s->map.pills[pi].armour == 15, "fix pill: 3 trees used");
		break;
	case 21:
		CHECK(k->mines == 39 && k->trees == 40 && game_terrain(mx, my) == TERRAIN_MINE_GRASS,
			"lgm mine: costs 1, a road on it refused");
		break;
	case 22:
		CHECK(m->inTank && !m->isDead && gManDied, "lgm shot: killed and replaced");
		break;
	case 23:
		CHECK(gForests[1] > gForests[0], "regrow: trees grew");
		break;
	case 24:
		CHECK(game_terrain((uint8_t)((k->x >> 8) - 2), (uint8_t)(k->y >> 8)) == TERRAIN_RIVER,
			"flood: the crater by the river filled");
		break;
	case 26:
		CHECK(k->destroyed, "mine wreck: the mine killed the tank");
		break;
	case 28:
		// Tanks nearer than a square push apart and nudge apart until dx
		// + dy is a square, so two pressing into each other settle a
		// little under a square apart. The one put on top is pushed out.
		CHECK(gDriveGap >= 224 && gLastGap >= 224, "collide: tanks pushed apart");
		break;
	case 29:
		CHECK(s->tanks[1].deaths == 1 && k->kills == 1 && s->tanks[1].kills == 0 &&
			!s->tanks[1].destroyed && s->tanks[1].armour == 40,
			"tank kill: shot dead, kill counted, back at a start");
		break;
	case 30:
		CHECK(s->tanks[1].armour < 40 && k->armour == 40, "pill near: the nearer tank shot");
		break;
	case 31:
		CHECK(s->tanks[1].armour < 40 && k->armour == 40, "pill ally: the ally passed over");
		break;
	case 32:
		CHECK((s->tanks[1].armour < 40 || s->tanks[1].destroyed) && k->armour == 40,
			"mine other: the other tank's mine hurt it");
		break;
	case 27:
		// The wreck moves as neutral, so it stops west of its own base and
		// craters there.
		CHECK(game_terrain((uint8_t)((k->x >> 8) - 1), (uint8_t)(k->y >> 8)) == TERRAIN_CRATER,
			"base wreck: stopped short of its own base");
		break;
	}
}

// WinBolo numbers: pill range 2048 (8 squares), a pill fires every 100
// ticks when calm and halves that when hit, a tournament tank's shells.
static void winbolo_numbers(const uint8_t *ev, uint32_t len)
{
	static GameState g;
	GameInput in = { 0, 0, 0, 0, 0 };
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

// Sounds the scripts must make.
static void sound_checks(uint8_t n)
{
	switch (n) {
	case 3:
		CHECK(gSounds[SND_BUBBLES] > 0, "sound: bubbles wading");
		break;
	case 4:
		CHECK(gSounds[SND_TANK_SINK] == 1, "sound: tank sinks");
		break;
	case 5:
		CHECK(gSounds[SND_SHOOT] > 0 && gSounds[SND_SHOT_BUILDING] > 0,
			"sound: shooting a building");
		break;
	case 7:
		CHECK(gSounds[SND_LAY_MINE] > 0 && gSounds[SND_MINE_EXPLOSION] > 0, "sound: mines");
		break;
	case 9:
		CHECK(gSounds[SND_BASE_CAPTURE] == 1, "sound: base captured");
		break;
	case 11:
		CHECK(gSounds[SND_HIT_TANK] > 0 && gSounds[SND_SHOT_TREE] > 0,
			"sound: tank hit by a pill, trees shot");
		break;
	case 15:
		CHECK(gSounds[SND_FARM_TREE] > 0, "sound: tree cut");
		break;
	case 16:
		CHECK(gSounds[SND_MAN_BUILDING] > 0, "sound: road built");
		break;
	case 22:
		CHECK(gSounds[SND_MAN_DYING] == 1, "sound: builder dies");
		break;
	case 25:
		CHECK(gSounds[SND_MINE_EXPLOSION] == 1 && gSounds[SND_BIG_EXPLOSION] == 1,
			"sound: small and big wrecks");
		break;
	case 26:
		// WinBolo quirk: a mine kill is always a big wreck.
		CHECK(gSounds[SND_BIG_EXPLOSION] == 1, "sound: mine kill leaves a big wreck");
		break;
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
		gManDied = 0;
		memset(gSounds, 0, sizeof(gSounds));
		c = script_run(&g, n, fact, NULL);
		{
			static const char *names[SND_KINDS] = {
				"shoot", "tree", "building", "hittank", "bubbles", "sink", "big",
				"mine", "laymine", "farm", "build", "mandie", "capture"
			};
			int j;

			printf("script %d sounds:", n);
			for (j = 0; j < SND_KINDS; j++)
				if (gSounds[j])
					printf(" %s %d", names[j], gSounds[j]);
			printf("\n");
		}
		sound_checks(n);
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

// Hidden mines: the player is shown their own mines and those they drive
// up to; the rules know a mine once a tank has seen it, so a builder will
// walk onto one nobody has seen.
static void hidden_mines(const uint8_t *ev, uint32_t len)
{
	static GameState g;
	GameSetup setup;
	GameInput in = { 0, 0, 0, 0, 0 };
	Tank *k = &g.tanks[0];
	uint8_t x, y;

	memset(&setup, 0, sizeof(setup));
	setup.numPlayers = 1;
	setup.hiddenMines = 1;
	CHECK(parse_buf(&g.map, ev, len) == MAP_OK, "hidden map");
	game_set_viewer(0);
	game_init(&g, 4242, &setup);
	// The road at 160,116 runs east; face east on it.
	k->x = 160 * 256 + 128;
	k->y = 116 * 256 + 128;
	k->angle = 64 * ANGLE_ONE;
	k->boat = BOAT_OFF;
	x = 160;
	y = 116;
	in.keys = IN_MINE;
	game_tick(&g, &in);
	CHECK(g_is_mine(x, y) && game_mine_shown(&g, x, y), "hidden: own mine shown");
	game_set_terrain(166, 116, TERRAIN_MINE_ROAD);
	CHECK(!game_mine_shown(&g, 166, 116) && !g_mine_known(&g, 166, 116),
		"hidden: other mine not shown or known");
	// Drive east until the tank sees it.
	in.keys = IN_FASTER;
	while (!game_mine_shown(&g, 166, 116) && g.tick < 400)
		game_tick(&g, &in);
	printf("hidden: mine at 166,116 seen at tick %u, tank at %u,%u\n", (unsigned)g.tick,
		k->x >> 8, k->y >> 8);
	CHECK(game_mine_shown(&g, 166, 116) && g_mine_known(&g, 166, 116) && (k->x >> 8) >= 162,
		"hidden: mine ahead seen");
}


// ---- save, resume and the time limit ----

static void run_ticks(GameState *g, uint32_t from, uint32_t to)
{
	GameInput in[GAME_MAX_TANKS];
	uint32_t t;

	for (t = from; t < to; t++) {
		memset(in, 0, sizeof(in));
		in[0].keys = (uint8_t)(IN_FASTER | ((t / 40) % 3 == 0 ? IN_LEFT : 0) | (t % 7 == 0 ? IN_FIRE : 0) |
			(t % 211 == 0 ? IN_MINE : 0));
		in[0].opts = OPT_AUTOSLOW;
		game_tick(g, in);
	}
}

// A game run straight through and the same game saved halfway, with
// another game played on the same map in between, then resumed must end
// in the same state: the checksums, the terrain and every byte.
static void save_resume(const uint8_t *ev, uint32_t len)
{
	static GameState a, b, c, saved;
	GameSetup setup;
	uint32_t sumA, sumB, terrA;

	memset(&setup, 0, sizeof(setup));
	setup.numPlayers = 1;
	setup.numBots = 3;
	setup.botLevel = BOT_NORMAL;
	setup.hiddenMines = 1;
	CHECK(parse_buf(&a.map, ev, len) == MAP_OK, "save: map");
	game_set_viewer(0);
	game_init(&a, 777, &setup);
	run_ticks(&a, 0, 3000);
	sumA = game_checksum(&a);
	terrA = game_terrain_checksum();

	CHECK(parse_buf(&b.map, ev, len) == MAP_OK, "save: map");
	game_init(&b, 777, &setup);
	run_ticks(&b, 0, 1500);
	CHECK(!mapstore_have_save(), "save: none yet");
	CHECK(mapstore_save() == 0 && mapstore_have_save(), "save: saved");
	memcpy(&saved, &b, sizeof(b));
	memset(&b, 0xA5, sizeof(b));
	CHECK(parse_buf(&c.map, ev, len) == MAP_OK, "save: map");
	game_init(&c, 999, &setup);
	run_ticks(&c, 0, 400);
	CHECK(mapstore_resume() == 0, "save: resumed");
	memcpy(&b, &saved, sizeof(b));
	run_ticks(&b, 1500, 3000);
	sumB = game_checksum(&b);
	printf("save and resume: straight %08lx, resumed %08lx, terrain %08lx %08lx\n", (unsigned long)sumA,
		(unsigned long)sumB, (unsigned long)terrA, (unsigned long)game_terrain_checksum());
	CHECK(sumA == sumB, "save: checksum matches");
	CHECK(terrA == game_terrain_checksum(), "save: terrain matches");
	CHECK(memcmp(&a, &b, sizeof(a)) == 0, "save: state matches byte for byte");
	mapstore_drop_save();
	CHECK(!mapstore_have_save(), "save: dropped");

	// The time limit: one minute is 3000 ticks, and the tick that reaches it is the last.
	setup.timeLimit = 1;
	CHECK(parse_buf(&a.map, ev, len) == MAP_OK, "limit: map");
	game_init(&a, 5, &setup);
	run_ticks(&a, 0, 2999);
	CHECK(!a.over, "limit: not over early");
	run_ticks(&a, 2999, 3000);
	CHECK(a.over && a.tick == 3000, "limit: over at 3000");
}


// ---- bots ----

static int gSoakGame;   // the soak game running, for debug prints

typedef struct BotStats {
	int captures, pickups, placed, drowned, sunk, kills, deaths, firstCapture;
	int pillHits, tankHits, byPill, byTank, byOther, pillsDown, noPath, botTicks, allyHits;
	// Ticks wading in a river off a boat, on a boat, boats boarded on land,
	// builder out of the tank alive and flying in, outings over 300 ticks, ticks out on a river,
	// squares entered.
	int wading, onBoat, boarded, manOut, manDead, manLong, manRiver, squares;
	int outFor[GAME_MAX_TANKS];
	int basesOwned[GAME_MAX_TANKS], pillsOwned[GAME_MAX_TANKS];
} BotStats;

// One tick with every tank a bot drives, counting what happened. A tank
// that dies on deep sea off a boat drowned.
static void bot_tick(GameState *g, BotStats *st)
{
	GameInput in[GAME_MAX_TANKS];
	GameSound snd[GAME_MAX_SOUNDS];
	uint8_t alive[GAME_MAX_TANKS], carry[GAME_MAX_TANKS], boat[GAME_MAX_TANKS], bump[GAME_MAX_TANKS], i, n;
	uint8_t near[GAME_MAX_TANKS], arm[GAME_MAX_TANKS], parm[MAP_MAX_PILLS], ally[GAME_MAX_TANKS];
	uint16_t sq[GAME_MAX_TANKS];

	memset(in, 0, sizeof(in));
	for (i = 0; i < g->map.numPills; i++)
		parm[i] = g->map.pills[i].armour;
	for (i = 0; i < g->numTanks; i++) {
		int j;

		// What shell, if any, is near the tank: 1 a pill's, 2 a tank's.
		near[i] = 0;
		ally[i] = 0;
		arm[i] = g->tanks[i].armour;
		for (j = 0; j < g->numShells; j++) {
			const Shell *h = &g->shells[j];

			if (abs((int)h->x - (int)g->tanks[i].x) < 400 && abs((int)h->y - (int)g->tanks[i].y) < 400 &&
			    h->owner != g->tanks[i].player)
			{
				near[i] = h->owner == GAME_NO_PLAYER ? 1 : 2;
				if (near[i] == 2 && game_allied(g, h->owner, g->tanks[i].player))
					ally[i] = 1;
			}
		}
		alive[i] = !g->tanks[i].destroyed && !g->tanks[i].deathWait;
		// Ticks a bot has somewhere to go but no path to follow yet.
		if (alive[i] && (g->botMask & (1 << i))) {
			st->botTicks++;
			st->noPath += g->bots[i].goal != 0 && g->bots[i].wayN == 0 && g->bots[i].mode == 0;
		}
		carry[i] = g->tanks[i].numCarry;
		if (alive[i]) {
			const Tank *k = &g->tanks[i];

			if (k->boat)
				st->onBoat++;
			else if (game_terrain(k->x >> 8, k->y >> 8) == TERRAIN_RIVER) {
				st->wading++;
				if (getenv("WADEDBG") && g->tick % 20 == 0)
					printf("  wade g%d %d %u,%u spd %d goal %u at %u,%u w%u mode %u man %u\n", gSoakGame, i, k->x >> 8, k->y >> 8, k->speed,
						g->bots[i].goal, g->bots[i].goalX, g->bots[i].goalY, g->bots[i].wayN, g->bots[i].mode, g->men[i].inTank);
			}
		}
		if (!g->men[i].inTank && g->men[i].isDead) {
			st->manDead++;
			st->outFor[i] = 0;
		} else if (!g->men[i].inTank) {
			st->manOut++;
			if (++st->outFor[i] == 300)
				st->manLong++;
			if (getenv("MANDBG") && st->outFor[i] % 300 == 0)
				printf("  man %d out %d: at %u,%u (%u) state %u dead %u ob %u act %u, tank at %u,%u boat %u dead %u goal %u at %u,%u w%u fails %u\n",
					i, st->outFor[i], g->men[i].x >> 8, g->men[i].y >> 8, game_terrain(g->men[i].x >> 8, g->men[i].y >> 8),
					g->men[i].state, g->men[i].isDead, g->men[i].obstructed, g->men[i].action,
					g->tanks[i].x >> 8, g->tanks[i].y >> 8, g->tanks[i].boat, g->tanks[i].destroyed || g->tanks[i].deathWait,
					g->bots[i].goal, g->bots[i].goalX, g->bots[i].goalY, g->bots[i].wayN, g->bots[i].fails);
			if (game_terrain(g->men[i].x >> 8, g->men[i].y >> 8) == TERRAIN_RIVER)
				st->manRiver++;
		} else {
			st->outFor[i] = 0;
		}
		sq[i] = (uint16_t)((g->tanks[i].x >> 8) | (g->tanks[i].y & 0xFF00));
		boat[i] = g->tanks[i].boat;
		bump[i] = g->tanks[i].bumpX || g->tanks[i].bumpY;
	}
	game_tick(g, in);
	n = game_take_sounds(snd, GAME_MAX_SOUNDS);
	for (i = 0; i < n; i++) {
		if (snd[i].kind == SND_BASE_CAPTURE) {
			st->captures++;
			if (st->firstCapture < 0)
				st->firstCapture = (int)g->tick;
		}
	}
	for (i = 0; i < g->map.numPills; i++)
		if (parm[i] > 0 && g->map.pills[i].armour == 0)
			st->pillsDown++;
	for (i = 0; i < g->numTanks; i++) {
		const Tank *k = &g->tanks[i];

		if (alive[i] && !k->destroyed && !k->deathWait) {
			st->boarded += k->boat && !boat[i];
			st->squares += sq[i] != (uint16_t)((k->x >> 8) | (k->y & 0xFF00));
		}
		if (k->numCarry > carry[i])
			st->pickups += k->numCarry - carry[i];
		else if (k->numCarry < carry[i] && !k->destroyed && !k->deathWait)
			st->placed += carry[i] - k->numCarry;
		if (alive[i] && (k->armour < arm[i] || k->destroyed || k->deathWait)) {
			if (near[i] == 1)
				st->pillHits++;
			else if (near[i] == 2)
				st->tankHits++;
			st->allyHits += ally[i];
		}
		if (alive[i] && (k->destroyed || k->deathWait)) {
			if (near[i] == 1)
				st->byPill++;
			else if (near[i] == 2)
				st->byTank++;
			else
				st->byOther++;
		}
		// Shot off a boat or knocked into the sea by a shell it sinks: that
		// isn't the bot's driving.
		if (alive[i] && k->destroyed && !k->boat &&
		    game_terrain(k->x >> 8, k->y >> 8) == TERRAIN_DEEP_SEA) {
			if (boat[i] || bump[i]) {
				st->sunk++;
				if (getenv("SUNKDBG"))
					printf("  sunk g%d tank %u at %u,%u boat %u bump %u near %u tick %u\n", gSoakGame, i, k->x >> 8, k->y >> 8, boat[i], bump[i], near[i], (unsigned)g->tick);
			} else {
				st->drowned++;
				printf("  drowned: tank %u at %u,%u tick %u\n", i, k->x >> 8, k->y >> 8,
					(unsigned)g->tick);
			}
		}
	}
}

static void bot_tally(const GameState *g, BotStats *st)
{
	int i, j;

	for (i = 0; i < g->numTanks; i++) {
		st->kills += g->tanks[i].kills;
		st->deaths += g->tanks[i].deaths;
		st->basesOwned[i] = st->pillsOwned[i] = 0;
		for (j = 0; j < g->map.numBases; j++)
			st->basesOwned[i] += g->map.bases[j].owner == g->tanks[i].player;
		for (j = 0; j < g->map.numPills; j++)
			st->pillsOwned[i] += g->map.pills[j].owner == g->tanks[i].player;
	}
}

static int gTeams;      // soak games in teams of two: tanks 0 and 1, 2 and 3...

static void bot_new(GameState *g, const uint8_t *ev, uint32_t len, uint32_t seed, int players,
	int bots, int level)
{
	GameSetup st;
	int i;

	memset(&st, 0, sizeof(st));
	for (i = 0; gTeams && i < GAME_MAX_PLAYERS; i++)
		st.allies[i] = (uint16_t)(1 << (i ^ 1));
	st.numPlayers = players;
	st.numBots = bots;
	st.botLevel = level;
	CHECK(parse_buf(&g->map, ev, len) == MAP_OK, "bot map");
	game_set_viewer(0);
	game_init(g, seed, &st);
}

static void bot_checks(const uint8_t *ev, uint32_t len)
{
	static GameState g;
	BotStats st;
	int t, armour0, deaths0, onBase = 0;

	// A lone bot takes a neutral base. The nearest one sits in a maze.
	memset(&st, 0, sizeof(st));
	st.firstCapture = -1;
	bot_new(&g, ev, len, 99, 1, 0, 0);
	game_set_bot(&g, 0, BOT_NORMAL);
	for (t = 0; t < 5000 && st.firstCapture < 0; t++) {
		if (getenv("BT1") && t % (getenv("BT2") ? 10 : 50) == 0 && t >= (getenv("BT2") ? atoi(getenv("BT2")) : 0))
			printf("  t%d man %u st %u ob %u at %u,%u spd %d ang %u way %u,%u who %u ph %u cur %u %u,%u arm %u goal %u tgt %u at %u,%u mode %u w%u fails %u skip %u\n", t, g.men[0].inTank, g.men[0].state, g.men[0].obstructed, g.men[0].x >> 8, g.men[0].y >> 8, g.tanks[0].speed, g.tanks[0].angle, g.bots[0].way[0][0], g.bots[0].way[0][1], g.search.who, g.search.phase, g.search.cur, g.tanks[0].x >> 8,
				g.tanks[0].y >> 8, g.tanks[0].armour, g.bots[0].goal, g.bots[0].target, g.bots[0].goalX,
				g.bots[0].goalY, g.bots[0].mode, g.bots[0].wayN, g.bots[0].fails, g.bots[0].skip);
		bot_tick(&g, &st);
	}
	printf("bot: first capture at tick %d\n", st.firstCapture);
	CHECK(st.firstCapture >= 0 && st.firstCapture < 5000, "bot: captures a neutral base in 5000 ticks");
	// Low on armour and shells, it goes back to a base and fills up.
	for (t = 0; t < 2000; t++)
		bot_tick(&g, &st);
	g.tanks[0].armour = 8;
	g.tanks[0].shells = 2;
	armour0 = g.tanks[0].armour;
	deaths0 = g.tanks[0].deaths;
	for (t = 0; t < 3000 && g.tanks[0].armour < 40; t++) {
		int b = g_base_at(&g, g.tanks[0].x >> 8, g.tanks[0].y >> 8);

		if (getenv("BT") && t % 10 == 0) printf("  t%d ang %u w%u %u,%u %u,%u arm %u goal %u tgt %u at %u,%u mode %u spd %d boat %u ter %u near %d\n", t, g.tanks[0].angle, g.bots[0].wayN, g.bots[0].way[0][0], g.bots[0].way[0][1], g.tanks[0].x>>8, g.tanks[0].y>>8, g.tanks[0].armour, g.bots[0].goal, g.bots[0].target, g.bots[0].goalX, g.bots[0].goalY, g.bots[0].mode, g.tanks[0].speed, g.tanks[0].boat, game_terrain(g.tanks[0].x>>8, g.tanks[0].y>>8), g.numShells);
		bot_tick(&g, &st);
		if (b >= 0 && g.map.bases[b].owner == g.tanks[0].player)
			onBase = 1;
	}
	printf("bot: refuel from armour %d to %u shells %u in %d ticks, on base %d, deaths %u\n", armour0,
		g.tanks[0].armour, g.tanks[0].shells, t, onBase, g.tanks[0].deaths - deaths0);
	CHECK(onBase && g.tanks[0].deaths == deaths0 && g.tanks[0].armour == 40,
		"bot: refuels at an own base when low");
	CHECK(st.drowned == 0, "bot: no drowning");

	// A far base across open ground: the bot drives at it all the way and
	// doesn't turn back at the edge of a search window.
	memset(&st, 0, sizeof(st));
	st.firstCapture = -1;
	bot_new(&g, ev, len, 99, 1, 0, 0);
	{
		int x, y, i, d, dMin = 255, back = 0;

		for (y = 40; y < 216; y++)
			for (x = 40; x < 216; x++)
				game_set_terrain((uint8_t)x, (uint8_t)y, TERRAIN_GRASS);
		for (i = 0; i < g.map.numPills; i++)
			g.map.pills[i].active = 0;
		for (i = 1; i < g.map.numBases; i++)
			g.map.bases[i].active = 0;
		g.map.bases[0].x = 190;
		g.map.bases[0].y = 140;
		g.map.bases[0].owner = MAP_NEUTRAL;
		g.tanks[0].x = (60 << 8) + 128;
		g.tanks[0].y = (120 << 8) + 128;
		game_set_bot(&g, 0, BOT_NORMAL);
		for (t = 0; t < 4000 && st.firstCapture < 0; t++) {
			bot_tick(&g, &st);
			if (getenv("FARDBG") && t % 100 == 0)
				printf("  t%d at %u,%u goal %u %u,%u w%u spd %d man %u\n", t, g.tanks[0].x >> 8,
					g.tanks[0].y >> 8, g.bots[0].goal, g.bots[0].goalX, g.bots[0].goalY,
					g.bots[0].wayN, g.tanks[0].speed, g.men[0].inTank);
			x = g.tanks[0].x >> 8;
			y = g.tanks[0].y >> 8;
			d = abs(x - 190) > abs(y - 140) ? abs(x - 190) : abs(y - 140);
			if (d < dMin)
				dMin = d;
			else if (d - dMin > back)
				back = d - dMin;
		}
		printf("bot: far base 130 squares off taken at tick %d, most lost ground %d squares\n",
			st.firstCapture, back);
		CHECK(st.firstCapture >= 0 && back <= 3, "bot: drives straight at a far goal in open ground");
	}

	// A player's tank is handed to a bot mid-game.
	memset(&st, 0, sizeof(st));
	st.firstCapture = -1;
	bot_new(&g, ev, len, 7, 2, 0, 0);
	{
		GameInput in[GAME_MAX_TANKS];

		memset(in, 0, sizeof(in));
		in[0].keys = IN_FASTER | IN_LEFT;
		for (t = 0; t < 150; t++)
			game_tick(&g, in);
		game_take_sounds(NULL, 0);
	}
	game_set_bot(&g, 0, BOT_HARD);
	for (t = 0; t < 5000 && st.firstCapture < 0; t++)
		bot_tick(&g, &st);
	printf("bot: takeover at tick 150, first capture at tick %d\n", st.firstCapture);
	CHECK(st.firstCapture >= 0 && g.map.bases[0].owner != 1, "bot: takes over a tank and plays");
	CHECK(st.drowned == 0, "bot: no drowning after takeover");
}

// Long bot games with different seeds. Prints what the bots did.
static void bot_soak(const uint8_t *ev, uint32_t len, int games, int ticks)
{
	static GameState g;
	int n, i, totalDrowned = 0;
	static const int kBots[] = { 4, 8, 3, 6, 2, 5 };
	BotStats tot;

	memset(&tot, 0, sizeof(tot));

	for (n = 0; n < games; n++) {
		BotStats st;
		int bots = kBots[n % 6], level = 1 + n % 3;

		gSoakGame = n;
		memset(&st, 0, sizeof(st));
		st.firstCapture = -1;
		// SOAK_SEED=N moves every game to other seeds.
		bot_new(&g, ev, len, (getenv("SOAK_SEED") ? atoi(getenv("SOAK_SEED")) : 1000) + n * 77, 1, bots - 1, level);
		game_set_bot(&g, 0, level);
		if (getenv("MD") && n == 0) {
			int x0, y0, x, y;

			sscanf(getenv("MD"), "%d,%d", &x0, &y0);
			for (y = y0; y < y0 + (getenv("MH") ? atoi(getenv("MH")) : 40); y++) {
				printf("%3d ", y);
				for (x = x0; x < x0 + (getenv("MW") ? atoi(getenv("MW")) : 60); x++) {
					int t = game_terrain(x, y);
					char c = "#rs=.Tu,hb" [t < 10 ? t : 0];

					if (t == 255) c = '~';
					else if (t >= 10) c = 'm';
					if (g_pill_at(&g, x, y) >= 0) c = 'P';
					if (g_base_at(&g, x, y) >= 0) c = 'B';
					putchar(c);
				}
				putchar('\n');
			}
		}
		for (i = 0; i < ticks; i++) {
			if (getenv("ST") && atoi(getenv("ST")) == n && i % (getenv("SE") ? atoi(getenv("SE")) : 25) == 0 && i >= (getenv("S0") ? atoi(getenv("S0")) : 0)) {
				int tk = getenv("STK") ? atoi(getenv("STK")) : 0;
				const Tank *k = &g.tanks[tk];
				const Bot *b = &g.bots[tk];

				printf("  t%d %u,%u b%u arm %u sh %u spd %d ang %u goal %u tgt %u (pa %u) at %u,%u mode %u w%u fails %u\n",
					i, k->x >> 8, k->y >> 8, k->boat, k->armour, k->shells, k->speed, k->angle >> 8, b->goal, b->target,
					b->goal == 2 ? g.map.pills[b->target].armour : 0, b->goalX, b->goalY, b->mode, b->wayN, k->numCarry); if (b->wayN) printf("     way %u,%u %u,%u x %u y %u search %u\n", b->way[0][0], b->way[0][1], b->way[1][0], b->way[1][1], k->x & 255, k->y & 255, g.search.who);
			}
			bot_tick(&g, &st);
		}
		bot_tally(&g, &st);
		printf("soak %d: %d bots level %d, %d ticks: captures %d (first %d) pickups %d kills %d deaths %d"
			" drowned %d sunk %d placed %d\n   bases/pills held:", n, bots, level, ticks, st.captures,
			st.firstCapture, st.pickups, st.kills, st.deaths, st.drowned, st.sunk, st.placed);
		for (i = 0; i < g.numTanks; i++)
			printf(" %d/%d", st.basesOwned[i], st.pillsOwned[i]);
		if (getenv("CARRY"))
			for (i = 0; i < g.numTanks; i++)
				if (!g.men[i].inTank)
					printf(" [man %d out, dead %u state %u ob %u wait %u at %u,%u, tank at %u,%u goal %u]", i, g.men[i].isDead,
						g.men[i].state, g.men[i].obstructed, g.men[i].waitTime, g.men[i].x >> 8, g.men[i].y >> 8, g.tanks[i].x >> 8, g.tanks[i].y >> 8, g.bots[i].goal);
		if (getenv("CARRY"))
			for (i = 0; i < g.numTanks; i++)
				if (g.tanks[i].numCarry)
					printf(" [tank %d carries %u, trees %u, goal %u]", i, g.tanks[i].numCarry,
						g.tanks[i].trees, g.bots[i].goal);
		printf("\n   pills down %d, hits by pills %d by tanks %d, deaths by pills %d tanks %d other %d,"
			" ally hits %d, no path %d%%\n", st.pillsDown, st.pillHits, st.tankHits, st.byPill, st.byTank,
			st.byOther, st.allyHits, st.botTicks ? st.noPath * 100 / st.botTicks : 0);
		printf("   wading %d%%, on a boat %d%%, boarded %d, builder out %d%% (on a river %d%%), outings over 300 ticks %d,"
			" squares a tick %d.%03d\n", st.botTicks ? st.wading * 100 / st.botTicks : 0,
			st.botTicks ? st.onBoat * 100 / st.botTicks : 0, st.boarded,
			st.botTicks ? st.manOut * 100 / st.botTicks : 0, st.botTicks ? st.manRiver * 100 / st.botTicks : 0,
			st.manLong, st.squares / (st.botTicks ? st.botTicks : 1),
			(int)((long)st.squares * 1000 / (st.botTicks ? st.botTicks : 1) % 1000));
		totalDrowned += st.drowned;
		tot.captures += st.captures;
		tot.pickups += st.pickups;
		tot.placed += st.placed;
		tot.kills += st.kills;
		tot.deaths += st.deaths;
		tot.sunk += st.sunk;
		tot.pillsDown += st.pillsDown;
		tot.byPill += st.byPill;
		tot.byTank += st.byTank;
		tot.byOther += st.byOther;
		tot.noPath += st.noPath;
		tot.botTicks += st.botTicks;
		tot.wading += st.wading;
		tot.onBoat += st.onBoat;
		tot.boarded += st.boarded;
		tot.manOut += st.manOut;
		tot.manDead += st.manDead;
		tot.manRiver += st.manRiver;
		tot.manLong += st.manLong;
		tot.squares += st.squares;
		tot.firstCapture += st.firstCapture < 0 ? ticks : st.firstCapture;
	}
	if (games > 0)
		printf("total over %d games: captures %d (first at %d on average) pickups %d placed %d kills %d deaths %d"
			" sunk %d pills down %d, deaths by pills %d tanks %d other %d\n"
			"   no path %d.%d%%, wading %d.%d%%, on a boat %d.%d%%, boarded %d, builder out %d.%d%% (on a river %d.%d%%, flying in %d.%d%%),"
			" outings over 300 ticks %d, squares per 1000 ticks %d\n",
			games, tot.captures, tot.firstCapture / games, tot.pickups, tot.placed, tot.kills, tot.deaths, tot.sunk,
			tot.pillsDown, tot.byPill, tot.byTank, tot.byOther,
			(int)((long)tot.noPath * 100 / tot.botTicks), (int)((long)tot.noPath * 1000 / tot.botTicks % 10),
			(int)((long)tot.wading * 100 / tot.botTicks), (int)((long)tot.wading * 1000 / tot.botTicks % 10),
			(int)((long)tot.onBoat * 100 / tot.botTicks), (int)((long)tot.onBoat * 1000 / tot.botTicks % 10),
			tot.boarded,
			(int)((long)tot.manOut * 100 / tot.botTicks), (int)((long)tot.manOut * 1000 / tot.botTicks % 10),
			(int)((long)tot.manRiver * 100 / tot.botTicks), (int)((long)tot.manRiver * 1000 / tot.botTicks % 10),
			(int)((long)tot.manDead * 100 / tot.botTicks), (int)((long)tot.manDead * 1000 / tot.botTicks % 10),
			tot.manLong, (int)((long)tot.squares * 1000 / tot.botTicks));
	CHECK(totalDrowned == 0, "soak: no drowning");
}

static double now_us(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec * 1e6 + ts.tv_nsec / 1e3;
}

// Times the bots apart from the rules, as the debug build does on the Palm,
// over n ticks from the end of script sc, or of a fresh game with bots.
static void bot_bench(const uint8_t *ev, uint32_t len, int sc, int bots, int n)
{
	static GameState c;
	static double each[20000];
	GameInput none[GAME_MAX_TANKS], keys[GAME_MAX_TANKS];
	double t0, t1, worst = 0, sum = 0;
	int i, nb = 0, over = 0;
	uint8_t mask;

	if (n > 20000)
		n = 20000;
	memset(none, 0, sizeof(none));
	if (sc >= 0) {
		CHECK(parse_buf(&c.map, ev, len) == MAP_OK, "bench map");
		script_run(&c, (uint8_t)sc, NULL, NULL);
	} else {
		bot_new(&c, ev, len, 4321, 1, bots, BOT_NORMAL);
		game_set_bot(&c, 0, BOT_NORMAL);
	}
	mask = c.botMask;
	for (i = 0; i < GAME_MAX_TANKS; i++)
		nb += (mask >> i) & 1;
	t0 = now_us();
	for (i = 0; i < n; i++) {
		double a = now_us(), d;

		game_bot_inputs(&c, none, keys);
		d = now_us() - a;
		each[i] = d;
		sum += d;
		if (d > worst)
			worst = d;
		c.botMask = 0;
		game_tick(&c, keys);
		c.botMask = mask;
	}
	t1 = now_us();
	for (i = 0; i < n; i++)
		over += each[i] > 4 * sum / n;
	printf("bench %s %d: %d tanks %d bots x%d: %.2f us a tick, bots %.2f us a bot a tick, worst bot tick %.0f us, "
		"%d ticks over 4x the mean\n", sc >= 0 ? "script" : "game", sc >= 0 ? sc : bots, c.numTanks, nb, n,
		(t1 - t0) / n, sum / n / nb, worst, over);
}

// One bot against each pill of the map in turn, the other pills gone and
// the nearest base its own. Prints how long the pill lasted.
static void bot_duel(const uint8_t *ev, uint32_t len, int level, int ticks)
{
	static GameState g;
	int pi, i, totalDown = 0, totalDeaths = 0, left = 0, hits = 0;

	for (pi = 0; pi < 16; pi++) {
		BotStats st;
		int bi = -1, bd = 9999, downAt = -1;

		memset(&st, 0, sizeof(st));
		bot_new(&g, ev, len, 500 + pi + (getenv("DS") ? atoi(getenv("DS")) * 1000 : 0), 1, 0, 0);
		if (pi >= g.map.numPills)
			break;
		for (i = 0; i < g.map.numPills; i++)
			if (i != pi)
				g.map.pills[i].active = 0;
		for (i = 0; i < g.map.numBases; i++) {
			int d = abs(g.map.bases[i].x - g.map.pills[pi].x) + abs(g.map.bases[i].y - g.map.pills[pi].y);

			if (d < bd && d > 10) {
				bd = d;
				bi = i;
			}
		}
		for (i = 0; i < g.map.numBases; i++)
			if (i != bi)
				g.map.bases[i].active = 0;
		g.map.bases[bi].owner = g.tanks[0].player;
		g.map.bases[bi].armour = 90;
		g.map.bases[bi].shells = 90;
		g.tanks[0].x = (uint16_t)((g.map.bases[bi].x << 8) + 128);
		g.tanks[0].y = (uint16_t)((g.map.bases[bi].y << 8) + 128);
		g.tanks[0].boat = 0;
		game_set_bot(&g, 0, level);
		for (i = 0; i < ticks && downAt < 0; i++) {
			if (getenv("DT") && atoi(getenv("DT")) == pi && i % 10 == 0) {
				const Tank *k = &g.tanks[0];
				const Bot *b = &g.bots[0];

				printf("  t%d %u.%02u,%u.%02u arm %u sh %u spd %d ang %u goal %u pa %u spd %u at %u,%u mode %u w%u\n",
					i, k->x >> 8, (k->x & 255) * 100 / 256, k->y >> 8, (k->y & 255) * 100 / 256, k->armour,
					k->shells, k->speed, k->angle >> 8, b->goal, g.map.pills[pi].armour, g.map.pills[pi].speed,
					b->goalX, b->goalY, b->mode, b->wayN);
			}
			bot_tick(&g, &st);
			if (g.map.pills[pi].armour == 0)
				downAt = i;
		}
		printf("duel pill %d at %d,%d base %d away: down at %d, armour %d, deaths %u, hits taken %d\n", pi,
			g.map.pills[pi].x, g.map.pills[pi].y, bd, downAt, g.map.pills[pi].armour, g.tanks[0].deaths,
			st.pillHits);
		totalDown += downAt >= 0;
		totalDeaths += g.tanks[0].deaths;
		left += g.map.pills[pi].armour;
		hits += st.pillHits;
	}
	printf("duel: %d pills down, %d deaths, pill armour left %d, hits taken %d\n", totalDown, totalDeaths,
		left, hits);
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
		if (argc > 1 && strcmp(argv[1], "--bench") == 0) {
			int n = argc > 2 ? atoi(argv[2]) : 2000;

			bot_bench(ev, len, 33, 0, n);
			bot_bench(ev, len, 34, 0, n);
			bot_bench(ev, len, -1, 3, n);
			bot_bench(ev, len, -1, 7, n);
			free(ev);
			return 0;
		}
		if (argc > 1 && strcmp(argv[1], "--duel") == 0) {
			bot_duel(ev, len, argc > 2 ? atoi(argv[2]) : 2, argc > 3 ? atoi(argv[3]) : 6000);
			free(ev);
			return 0;
		}
		if (argc > 1 && strcmp(argv[1], "--soak") == 0) {
			int games = argc > 2 ? atoi(argv[2]) : 6, ticks = argc > 3 ? atoi(argv[3]) : 30000;

			gTeams = argc > 4 && strcmp(argv[4], "teams") == 0;

			bot_soak(ev, len, games, ticks);
			free(ev);
			printf(gFails ? "%d checks failed\n" : "ok\n", gFails);
			return gFails != 0;
		}
		if (argc > 1 && strcmp(argv[1], "--lockstep") == 0) {
			int seeds = argc > 2 ? atoi(argv[2]) : 1, ticks = argc > 3 ? atoi(argv[3]) : 6000;

			gFails += lockstep_tests(ev, len, seeds, (uint32_t)ticks);
			free(ev);
			printf(gFails ? "%d checks failed\n" : "ok\n", gFails);
			return gFails != 0;
		}
		scripts(ev, len);
		winbolo_numbers(ev, len);
		hidden_mines(ev, len);
		bot_checks(ev, len);
		save_resume(ev, len);
		gFails += lockstep_tests(ev, len, 1, 3000);
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
