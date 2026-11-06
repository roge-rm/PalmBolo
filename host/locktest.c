// Lockstep tests: two to four copies of the game, each on its own terrain
// bank, play through the loopback link with delays, lost and reordered
// frames, a player who goes silent or leaves, and a deliberate desync.
// Every copy still playing must end with the same state. Some games start
// with joiners that lack the map and get it from the host, and some lose
// a player from the lobby or turn away a late joiner.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/game/game.h"
#include "../src/game/mapstore.h"
#include "../src/game/util.h"
#include "../src/net/link.h"
#include "../src/net/lockstep.h"

void mapstore_host_bank(int n);

static int gLsFails;
#define LCHECK(cond, ...) do { if (!(cond)) { printf("FAIL lockstep: " __VA_ARGS__); printf("\n"); gLsFails++; } } while (0)

#define DEVS 4

typedef struct Dev {
	GameState g;
	Lockstep ls;
	Link link;
	uint32_t due;           // ms the next tick is due
	uint16_t tickMs;        // its pace
	uint32_t rng;           // its player's keys
	uint8_t hold, holdLeft, opts;
	int gone;               // switched off
	int left;               // left the game
	int set;                // game set up
	uint8_t waitSeen;       // slots ls_waiting named
	uint8_t *got;           // the map as it came from the host
	uint32_t gotLen;
	uint32_t pauseUntil;    // not polled until then, as with a menu open
	int paused;             // has been paused
	int fin;                // timed game: over, waited and left
	uint32_t overAt;        // when it reached the end
} Dev;

typedef struct Scenario {
	const char *name;
	int devs, bots;
	uint16_t minMs, maxMs, lossPm;
	uint8_t reorder;
	uint8_t delay;          // 0: picked from the round trip
	int goneDev, goneAt;    // switched off at this tick, -1 for none
	int leaveDev, leaveAt;
	int badDev, badAt;      // state changed by hand at this tick
	int slowDev;
	uint16_t slowMs;
	int teams, hidden;
	int fetch;              // joiners lack the map
	int lobbyOut, lobbyHow; // device leaving the lobby (0 none): 0 says so, 1 goes silent
	int late;               // someone tries to join once the game has started
	int pauseDev, pauseAt;  // not polled from this tick for pauseMs, -1 for none
	uint32_t pauseMs;
	int timed;              // time limit in minutes: every device runs to the end
} Scenario;

static Dev gDev[DEVS];

static uint32_t rnd(uint32_t *r)
{
	*r ^= *r << 13;
	*r ^= *r >> 17;
	*r ^= *r << 5;
	return *r;
}

// The player at a device: keys held for a while, gunsight taps and now
// and then a builder order near the tank, as a person might.
static void make_input(Dev *d, GameInput *in)
{
	static const uint8_t kCombos[] = {
		IN_FASTER, IN_FASTER | IN_LEFT, IN_FASTER | IN_RIGHT, IN_SLOWER, IN_FASTER | IN_FIRE,
		IN_FIRE, IN_LEFT | IN_FIRE, 0, IN_MINE | IN_FASTER, IN_RIGHT
	};
	uint32_t r = rnd(&d->rng);
	const Tank *k = &d->g.tanks[d->ls.slot];

	if (!d->holdLeft) {
		d->hold = kCombos[r % sizeof(kCombos)];
		d->holdLeft = (uint8_t)(5 + (r >> 8) % 60);
	}
	d->holdLeft--;
	memset(in, 0, sizeof(*in));
	in->keys = d->hold;
	in->opts = d->opts;
	if ((r >> 16) % 97 == 0)
		in->keys |= (r >> 24) & 1 ? IN_SIGHT_NEAR : IN_SIGHT_FAR;
	if ((r >> 12) % 300 == 0) {
		in->build = (uint8_t)(BUILD_TREE + (r >> 20) % 5);
		in->buildX = (uint8_t)((k->x >> 8) + (r >> 4) % 7 - 3);
		in->buildY = (uint8_t)((k->y >> 8) + (r >> 7) % 7 - 3);
	}
}

// The host's map bytes, and a joiner keeping them as they come.
static void map_get(void *ctx, uint16_t off, uint8_t *dst, uint8_t n)
{
	memcpy(dst, (const uint8_t *)ctx + off, n);
}

static uint8_t map_put(void *ctx, uint16_t off, const uint8_t *src, uint8_t n)
{
	Dev *d = (Dev *)ctx;

	if (off != d->gotLen || off + n > d->ls.start.mapLen)
		return 1;
	memcpy(d->got + off, src, n);
	d->gotLen += n;
	return 0;
}

// Joiners in the lobby see what the host has.
static int lobby_seen(int n)
{
	const Lockstep *h = &gDev[0].ls;
	int i, k;

	for (i = 1; i < n; i++) {
		const Lockstep *ls = &gDev[i].ls;

		if (gDev[i].gone)
			continue;
		if (!ls->inLobby || ls->seated != h->seated || ls->slot >= LS_SLOTS ||
		    h->node[ls->slot] != gDev[i].link.node || strcmp(ls->mapName, h->mapName) != 0)
			return 0;
		for (k = 0; k < LS_SLOTS; k++)
			if (((h->seated >> k) & 1) && strcmp(ls->start.names[k], h->start.names[k]) != 0)
				return 0;
	}
	return 1;
}

static void set_up_game(Dev *d, int bank, const uint8_t *ev, uint32_t len)
{
	GameSetup setup;
	MapSource ms;
	MapMemory mm;

	mapstore_host_bank(bank);
	mapstore_open(TERRAIN_DEEP_SEA);
	map_source_memory(&ms, &mm, ev, len);
	LCHECK(map_parse(&d->g.map, &ms) == MAP_OK, "map");
	ls_setup(&d->ls.start, &setup);
	game_init(&d->g, d->ls.start.seed, &setup);
	d->set = 1;
}

static void poll_all(int n, uint32_t t)
{
	int i;

	loop_time(t);
	for (i = 0; i < n; i++)
		if (!gDev[i].gone && t >= gDev[i].pauseUntil)
			ls_poll(&gDev[i].ls, t);
}

// Runs one game to ticks. Returns 0 if it ran as the scenario expects.
static int run(const Scenario *sc, const uint8_t *ev, uint32_t len, uint32_t seed, uint32_t ticks)
{
	int i, n = sc->devs, live, fails = gLsFails, desyncs = 0;
	uint32_t t = 0, sums[DEVS], terr[DEVS], compared = 1 << 30, startAt = 0, mapAt = 0;
	static Dev late;
	uint16_t rtt;
	LsStart st;
	char name[24];

	loop_reset(seed * 2654435761u + 1, sc->minMs, sc->maxMs, sc->lossPm, sc->reorder);
	memset(gDev, 0, sizeof(gDev));
	for (i = 0; i < n; i++) {
		Dev *d = &gDev[i];

		LCHECK(link_open(&d->link, LINK_LOOP, i ? LINK_ROLE_JOIN : LINK_ROLE_HOST) == 0 &&
			d->link.node == i, "link open");
		snprintf(name, sizeof(name), "player%d", i);
		if (i)
			ls_join_open(&d->ls, &d->link, name);
		else
			ls_host_open(&d->ls, &d->link, name);
		d->rng = seed * 97 + i * 1013 + 7;
		d->opts = (uint8_t)(rnd(&d->rng) & 3);
		d->tickMs = (uint16_t)(i == sc->slowDev ? sc->slowMs : 20);
	}
	ls_host_lobby(&gDev[0].ls, "Everard Island", (uint8_t)sc->bots, (uint8_t)sc->teams, map_get,
		(void *)ev);
	// The lobby, and three seconds of pings for the round trip. Every
	// joiner must see who is seated. One may leave the lobby after 1.5 s.
	for (;; t++) {
		int seated = sc->lobbyOut && t >= 1500 ? n - 1 : n;

		poll_all(n, t);
		if (sc->lobbyOut && t == 1500) {
			Dev *d = &gDev[sc->lobbyOut];

			if (sc->lobbyHow) {
				d->gone = 1;
				loop_cut((uint8_t)sc->lobbyOut, 1);
			} else {
				ls_leave(&d->ls);
			}
		}
		if (sc->lobbyOut && !sc->lobbyHow && t > 1500 && !gDev[sc->lobbyOut].gone &&
		    !gDev[sc->lobbyOut].ls.stopLeft) {
			gDev[sc->lobbyOut].gone = 1;
			loop_cut((uint8_t)sc->lobbyOut, 1);
		}
		if (t >= 3000 && (!sc->lobbyOut || t >= 1500 + LS_DROP_MS + 500) &&
		    gDev[0].ls.seated == (1 << seated) - 1 && lobby_seen(n))
			break;
		if (t > 30000) {
			LCHECK(0, "%s: lobby never filled (seated %02x)", sc->name, gDev[0].ls.seated);
			return 1;
		}
	}
	rtt = ls_rtt(&gDev[0].ls);
	memset(&st, 0, sizeof(st));
	st.seed = seed * 31 + 5;
	st.mapSum = ~util_crc(0xFFFFFFFFUL, ev, (uint16_t)len);
	st.mapLen = (uint16_t)len;
	st.numBots = (uint8_t)sc->bots;
	st.botLevel = BOT_NORMAL;
	st.hiddenMines = (uint8_t)sc->hidden;
	st.timeLimit = (uint8_t)sc->timed;
	st.delay = sc->delay ? sc->delay : ls_delay_for(rtt);
	if (sc->teams)
		for (i = 0; i < LS_SLOTS; i++)
			st.allies[i] = (uint8_t)(1 << (i ^ 1));
	ls_host_start(&gDev[0].ls, &st);
	set_up_game(&gDev[0], 0, ev, len);
	startAt = t;
	if (sc->late) {
		LCHECK(link_open(&late.link, LINK_LOOP, LINK_ROLE_JOIN) == 0, "late link");
		ls_join_open(&late.ls, &late.link, "late");
	}

	for (;; t++) {
		poll_all(n, t);
		if (sc->late)
			ls_poll(&late.ls, t);
		live = 0;
		for (i = 0; i < n; i++) {
			Dev *d = &gDev[i];
			GameInput in;

			if (d->gone || t < d->pauseUntil || d->fin)
				continue;
			if (sc->timed && d->g.over && d->ls.state == LS_PLAYING) {
				// The end: wait for the others, then leave. They all get
				// there well before the longest wait.
				if (!d->ls.finishing)
					d->overAt = t;
				ls_finish(&d->ls, &d->g);
				if (ls_finished(&d->ls)) {
					LCHECK(t - d->overAt < LS_END_MS / 2, "%s seed %u: device %d waited %u ms at the end",
						sc->name, (unsigned)seed, i, (unsigned)(t - d->overAt));
					ls_leave(&d->ls);
					d->fin = 1;
				} else {
					live++;
				}
				continue;
			}
			if (d->left) {
				// Gone once its LEAVEs are out.
				if (!d->ls.stopLeft) {
					d->gone = 1;
					loop_cut((uint8_t)i, 1);
				}
				continue;
			}
			if (d->ls.state == LS_STARTING && !d->ls.host) {
				int j;

				if (sc->fetch && d->ls.fetch == LS_MAP_NONE) {
					d->got = malloc(d->ls.start.mapLen);
					d->gotLen = 0;
					ls_join_fetch(&d->ls, map_put, d);
				}
				if (!sc->fetch || d->ls.fetch == LS_MAP_GOT) {
					for (j = 0; j < gDev[0].ls.start.numPlayers; j++)
						LCHECK(strcmp(d->ls.start.names[j], gDev[0].ls.start.names[j]) == 0, "%s: name %d [%s] [%s]", sc->name, j, d->ls.start.names[j], gDev[0].ls.start.names[j]);
					if (sc->fetch) {
						LCHECK(d->gotLen == len && memcmp(d->got, ev, len) == 0,
							"%s seed %u: device %d map bytes differ", sc->name, (unsigned)seed, i);
						set_up_game(d, i, d->got, d->gotLen);
					} else {
						set_up_game(d, i, ev, len);
					}
					ls_join_ready(&d->ls);
					if (sc->fetch)
						mapAt = t;      // the last joiner to have it
				}
			}
			if (d->ls.state == LS_STOPPED)
				continue;
			if (d->g.tick < ticks || sc->timed)
				live++;
			if (d->due + 200 < t)
				d->due = t - 200;
			while (d->set && (sc->timed ? !d->g.over : d->g.tick < ticks) && t >= d->due &&
			       ls_ready(&d->ls)) {
				mapstore_host_bank(i);
				game_set_viewer(d->ls.slot);
				if (i == sc->goneDev && (int)d->g.tick >= sc->goneAt) {
					d->gone = 1;
					loop_cut((uint8_t)i, 1);
					break;
				}
				if (i == sc->pauseDev && !d->paused && (int)d->g.tick >= sc->pauseAt) {
					d->paused = 1;
					d->pauseUntil = t + sc->pauseMs;
					break;
				}
				if (i == sc->leaveDev && (int)d->g.tick >= sc->leaveAt) {
					ls_leave(&d->ls);
					d->left = 1;
					break;
				}
				if (i == sc->badDev && (int)d->g.tick == sc->badAt)
					d->g.rng ^= 0x00A50000UL;
				make_input(d, &in);
				ls_tick(&d->ls, &d->g, &in);
				d->due += d->tickMs + rnd(&d->rng) % 3;
			}
		}
		poll_all(n, t);
		for (i = 0; i < n; i++)
			if (!gDev[i].gone)
				gDev[i].waitSeen |= ls_waiting(&gDev[i].ls);
		if (!live)
			break;
		if (t > 3000 + (sc->timed ? (uint32_t)sc->timed * 3000 : ticks) * 200) {
			LCHECK(0, "%s seed %u: no end by %u ms", sc->name, (unsigned)seed, (unsigned)t);
			break;
		}
	}
	// Let the last frames out, so stops reach everyone.
	{
		uint32_t end = t + 5000;

		for (; t < end; t++)
			poll_all(n, t);
	}
	if (sc->late) {
		LCHECK(late.ls.state == LS_STOPPED && late.ls.stop == LS_STOP_FULL, "%s: late joiner state %d stop %d",
			sc->name, late.ls.state, late.ls.stop);
		link_close(&late.link);
	}
	for (i = 0; i < n; i++) {
		free(gDev[i].got);
		gDev[i].got = NULL;
	}

	for (i = 0; i < n; i++) {
		Dev *d = &gDev[i];

		mapstore_host_bank(i);
		sums[i] = game_checksum(&d->g);
		terr[i] = game_terrain_checksum();
		if (d->ls.stop == LS_STOP_DESYNC)
			desyncs++;
	}
	if (sc->badDev >= 0) {
		uint32_t first = ((uint32_t)sc->badAt / (LS_TICKS * LS_SUM_TURNS) + 1) * LS_TICKS * LS_SUM_TURNS;

		for (i = 0; i < n; i++) {
			const Lockstep *ls = &gDev[i].ls;

			LCHECK(ls->state == LS_STOPPED && ls->stop == LS_STOP_DESYNC, "%s: device %d stopped (state %d stop %d)",
				sc->name, i, ls->state, ls->stop);
			LCHECK(ls->badTick == first, "%s: device %d desync at tick %u, expected %u", sc->name, i,
				(unsigned)ls->badTick, (unsigned)first);
		}
		printf("lockstep %-10s seed %u: %d devices, delay %u, rtt %u ms: desync caught at tick %u "
			"(changed at %d), slot %u, %08lx vs %08lx\n", sc->name, (unsigned)seed, n,
			(unsigned)gDev[0].ls.start.delay, (unsigned)rtt, (unsigned)gDev[0].ls.badTick, sc->badAt,
			(unsigned)gDev[0].ls.badSlot, (unsigned long)gDev[0].ls.badLocal,
			(unsigned long)gDev[0].ls.badRemote);
		return gLsFails != fails;
	}
	if (sc->pauseDev >= 0) {
		// Back from the pause, it learns a bot has its tank and stops.
		Dev *d = &gDev[sc->pauseDev];

		LCHECK(d->ls.state == LS_STOPPED && d->ls.stop == LS_STOP_DROPPED, "%s seed %u: paused device "
			"state %d stop %d at tick %u", sc->name, (unsigned)seed, d->ls.state, d->ls.stop,
			(unsigned)d->g.tick);
		d->gone = 1;
	}
	for (i = 0; i < n; i++) {
		const Dev *d = &gDev[i];

		if (d->gone)
			continue;
		if (sc->timed) {
			// Everyone reached the end and left without a stop or a bot.
			LCHECK(d->fin && d->g.over && d->ls.state == LS_STOPPED &&
				d->ls.stop == (i ? LS_STOP_NONE : LS_STOP_END), "%s seed %u: device %d fin %d over %d "
				"state %d stop %d ended %d", sc->name, (unsigned)seed, i, d->fin, d->g.over, d->ls.state,
				d->ls.stop, d->ls.ended);
			LCHECK(d->g.tick == (uint32_t)sc->timed * 60 * GAME_TICKS_PER_SECOND, "%s seed %u: device %d "
				"ended at tick %u", sc->name, (unsigned)seed, i, (unsigned)d->g.tick);
			LCHECK(d->g.botMask == gDev[0].g.botMask && !(d->g.botMask & ((1 << n) - 1)),
				"%s seed %u: device %d bots %02x", sc->name, (unsigned)seed, i, d->g.botMask);
		} else {
			LCHECK(d->ls.state == LS_PLAYING, "%s seed %u: device %d state %d stop %d", sc->name,
				(unsigned)seed, i, d->ls.state, d->ls.stop);
			LCHECK(d->g.tick == ticks, "%s seed %u: device %d at tick %u", sc->name, (unsigned)seed, i,
				(unsigned)d->g.tick);
		}
		LCHECK(sums[i] == sums[0] && terr[i] == terr[0], "%s seed %u: device %d state %08lx terrain %08lx, "
			"host %08lx %08lx", sc->name, (unsigned)seed, i, (unsigned long)sums[i],
			(unsigned long)terr[i], (unsigned long)sums[0], (unsigned long)terr[0]);
		LCHECK(memcmp(&d->g, &gDev[0].g, sizeof(GameState)) == 0, "%s seed %u: device %d state bytes differ",
			sc->name, (unsigned)seed, i);
		if (d->ls.sumsCompared < compared)
			compared = d->ls.sumsCompared;
	}
	LCHECK(compared >= (sc->timed ? gDev[0].g.tick : ticks) / (LS_TICKS * LS_SUM_TURNS) - 1,
		"%s seed %u: only %u checksums compared", sc->name, (unsigned)seed, (unsigned)compared);
	if (sc->goneDev >= 0 || sc->leaveDev >= 0 || sc->pauseDev >= 0) {
		int who = gDev[sc->goneDev >= 0 ? sc->goneDev : sc->leaveDev >= 0 ? sc->leaveDev : sc->pauseDev].ls.slot;

		LCHECK((gDev[0].g.botMask >> who) & 1, "%s seed %u: tank %d not handed to a bot", sc->name,
			(unsigned)seed, who);
		// Everyone was shown who they waited for.
		for (i = 0; sc->goneDev >= 0 && i < n; i++)
			LCHECK(gDev[i].gone || ((gDev[i].waitSeen >> who) & 1), "%s seed %u: device %d waited for %02x",
				sc->name, (unsigned)seed, i, gDev[i].waitSeen);
	}
	LCHECK(desyncs == 0, "%s: %d desyncs", sc->name, desyncs);
	if (sc->fetch)
		printf("lockstep %-10s seed %u: map of %u bytes in %u ms\n", sc->name, (unsigned)seed,
			(unsigned)len, (unsigned)(mapAt - startAt));
	printf("lockstep %-10s seed %u: %d devices, delay %u, rtt %u ms, %u ticks in %u.%u s, "
		"%u checks, frames %u lost %u, bots %02x, state %08lx terrain %08lx\n",
		sc->name, (unsigned)seed, n, (unsigned)gDev[0].ls.start.delay, (unsigned)rtt,
		(unsigned)gDev[0].g.tick, (unsigned)(t / 1000), (unsigned)(t % 1000 / 100), (unsigned)compared,
		(unsigned)loop_sent(), (unsigned)loop_lost(), (unsigned)gDev[0].g.botMask,
		(unsigned long)sums[0], (unsigned long)terr[0]);
	return gLsFails != fails;
}

//                name          devs bots min  max loss reord delay gone    leave   bad      slow    teams hidden
static const Scenario kScenarios[] = {
	{ "ir",         2, 2,  5,  40,   0, 0, 0, -1, 0,  -1, 0,  -1, 0,     -1, 0,  0, 0,  0, 0, 0, 0, -1, 0, 0, 0 },
	{ "ir-slow",    2, 3, 10,  60,   0, 0, 0, -1, 0,  -1, 0,  -1, 0,      1, 26, 0, 1,  0, 0, 0, 0, -1, 0, 0, 0 },
	{ "udp",        4, 2,  0, 150, 100, 1, 0, -1, 0,  -1, 0,  -1, 0,     -1, 0,  1, 0,  0, 0, 0, 0, -1, 0, 0, 0 },
	{ "udp-bad",    3, 0, 20, 300, 250, 1, 0, -1, 0,  -1, 0,  -1, 0,      2, 24, 0, 1,  0, 0, 0, 0, -1, 0, 0, 0 },
	{ "drop",       3, 1,  5,  60,  50, 1, 0,  2, 1500, -1, 0, -1, 0,    -1, 0,  0, 0,  0, 0, 0, 0, -1, 0, 0, 0 },
	{ "leave",      4, 0,  5,  40,   0, 0, 3, -1, 0,   1, 2000, -1, 0,   -1, 0,  0, 0,  0, 0, 0, 0, -1, 0, 0, 0 },
	{ "desync",     3, 1,  5,  40,   0, 0, 0, -1, 0,  -1, 0,   1, 1000,  -1, 0,  0, 0,  0, 0, 0, 0, -1, 0, 0, 0 },
	{ "desync-h",   2, 0,  5,  80,  50, 1, 0, -1, 0,  -1, 0,   0, 700,   -1, 0,  0, 0,  0, 0, 0, 0, -1, 0, 0, 0 },
	//                                                                                    fetch lobbyOut how late pause at ms timed
	{ "map-ir",     2, 1,  5,  40,   0, 0, 0, -1, 0,  -1, 0,  -1, 0,     -1, 0,  0, 0,  1, 0, 0, 1, -1, 0, 0, 0 },
	{ "map-udp",    3, 0,  0, 150, 150, 1, 0, -1, 0,  -1, 0,  -1, 0,     -1, 0,  1, 0,  1, 2, 0, 0, -1, 0, 0, 0 },
	{ "lobby-drop", 4, 1, 10,  60,   0, 0, 0, -1, 0,  -1, 0,  -1, 0,     -1, 0,  0, 1,  1, 1, 1, 1, -1, 0, 0, 0 },
	{ "pause",      3, 1,  5,  40,   0, 0, 0, -1, 0,  -1, 0,  -1, 0,     -1, 0,  0, 0,  0, 0, 0, 0,  2, 1500, 4000, 0 },
	{ "pause-udp",  3, 0,  0, 150, 100, 1, 0, -1, 0,  -1, 0,  -1, 0,     -1, 0,  0, 0,  0, 0, 0, 0,  1, 1000, 5000, 0 },
	{ "timed",      3, 1,  5,  60,  50, 1, 0, -1, 0,  -1, 0,  -1, 0,      2, 24, 0, 0,  0, 0, 0, 0, -1, 0,    0,    1 },
	{ "timed-ir",   2, 2,  5,  40,   0, 0, 0, -1, 0,  -1, 0,  -1, 0,     -1, 0,  1, 0,  0, 0, 0, 0, -1, 0,    0,    1 },
};

#define BIG_MAP 20000

// Runs every scenario for seeds 1..seeds, games of ticks (the desync ones
// stop early). Returns the number of failed checks.
int lockstep_tests(const uint8_t *ev, uint32_t len, int seeds, uint32_t ticks)
{
	int s;
	unsigned i;
	uint8_t *big;
	uint32_t k, r;

	gLsFails = 0;
	{
		// Inputs come back as they went.
		LCHECK(ls_delay_for(0) == 2 && ls_delay_for(100) == 3 && ls_delay_for(2000) == LS_MAX_DELAY,
			"delay for rtt %u %u %u", ls_delay_for(0), ls_delay_for(100), ls_delay_for(2000));
	}
	// The map sent is Everard Island with bytes after it, which the parser
	// leaves alone, to make it the size of a big map.
	big = malloc(BIG_MAP);
	memcpy(big, ev, len);
	for (k = len, r = 99; k < BIG_MAP; k++)
		big[k] = (uint8_t)rnd(&r);
	// LOCK_ONLY=name runs one game, LOCK_SEED=n from that seed.
	for (s = getenv("LOCK_SEED") ? atoi(getenv("LOCK_SEED")) : 1; s <= seeds; s++)
		for (i = 0; i < sizeof(kScenarios) / sizeof(kScenarios[0]); i++)
			if (!getenv("LOCK_ONLY") || strcmp(getenv("LOCK_ONLY"), kScenarios[i].name) == 0)
				run(&kScenarios[i], kScenarios[i].fetch ? big : ev, kScenarios[i].fetch ? BIG_MAP : len,
					(uint32_t)s, ticks);
	free(big);
	mapstore_host_bank(0);
	return gLsFails;
}
