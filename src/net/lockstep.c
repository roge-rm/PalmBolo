// Lockstep network play: turns, input delay, checksums and handing silent
// players' tanks to bots. See lockstep.h and doc/lockstep.md.
//
// The ring of LS_WIN turns holds turns from LS_WIN / 2 behind the turn
// being run to LS_WIN / 2 ahead. Inputs come at most twice the delay ahead
// (a joiner can be the delay ahead of the host and makes its input the
// delay ahead of itself), so LS_MAX_DELAY must stay under LS_WIN / 4.
#include "lockstep.h"
#include "netutil.h"
#include "../game/mapstore.h"
#include "../game/util.h"

#define HALF            (LS_WIN / 2)
#define RING(t)         ((uint8_t)((t) & (LS_WIN - 1)))
#define KEEPALIVE_MS    250
#define PING_MS         1000
#define HELLO_MS        500
#define STOP_SENDS      3
#define ROSTER_MS       1000
#define MAPGET_MS       150

enum {
	MSG_HELLO = 1, MSG_START, MSG_NAME, MSG_READY, MSG_INPUT, MSG_TURNS,
	MSG_PING, MSG_PONG, MSG_STOP, MSG_LEAVE,
	MSG_MAPGET = 16, MSG_MAPDATA, MSG_SEATS
};

// Flags in the count byte of INPUT and TURNS.
#define F_SUM   0x80
#define F_WAIT  0x40
#define F_COUNT 0x3F

#define START_LEN 30     // then the map's name, up to LS_MAPNAME - 1 bytes
#define NAME_START 0x80  // in NAME's slot byte: a START name, not the lobby's
#define SUM_LEN 8

static uint8_t SEC_NET bits(uint8_t m)
{
	uint8_t n = 0;

	for (; m; m = (uint8_t)(m & (m - 1)))
		n++;
	return n;
}

// The ring entry of turn t, or 255 if it holds another turn.
static uint8_t SEC_NET entry(const Lockstep *ls, uint32_t t)
{
	uint8_t e = RING(t);

	return ls->ringTurn[e] == t ? e : 255;
}

// The ring entry of turn t, emptied first if it held another turn.
static uint8_t SEC_NET claim(Lockstep *ls, uint32_t t)
{
	uint8_t e = RING(t);

	if (ls->ringTurn[e] != t) {
		ls->ringTurn[e] = t;
		ls->have[e] = ls->done[e] = ls->act[e] = ls->handover[e] = 0;
		net_zero(ls->in[e], sizeof(ls->in[e]));
	}
	return e;
}

// A turn number sent as its low 16 bits, near the turn being run. Returns
// 0 if it is more than LS_WIN / 2 away.
static uint8_t SEC_NET widen(const Lockstep *ls, uint16_t w, uint32_t *t)
{
	int16_t d = (int16_t)(w - (uint16_t)ls->turn);

	if (d < -HALF || d >= HALF || (d < 0 && ls->turn < (uint32_t)-d))
		return 0;
	*t = ls->turn + d;
	return 1;
}

// ---- inputs on the wire ----
// A head byte: options in bits 0-1, the builder order in bits 2-4, bit 5
// set when all ticks have the same keys. Then one key byte or four, then
// the order's square if there is an order.

static uint8_t SEC_NET in_same(const LsInput *p)
{
	return p->keys[0] == p->keys[1] && p->keys[0] == p->keys[2] && p->keys[0] == p->keys[3];
}

static uint8_t SEC_NET in_size(const LsInput *p)
{
	return (uint8_t)(1 + (in_same(p) ? 1 : LS_TICKS) + (p->build ? 2 : 0));
}

static uint8_t SEC_NET in_put(uint8_t *b, const LsInput *p)
{
	uint8_t n = 1, i, same = in_same(p);

	b[0] = (uint8_t)((p->opts & 3) | ((p->build & 7) << 2) | (same ? 0x20 : 0));
	if (same)
		b[n++] = p->keys[0];
	else
		for (i = 0; i < LS_TICKS; i++)
			b[n++] = p->keys[i];
	if (p->build) {
		b[n++] = p->buildX;
		b[n++] = p->buildY;
	}
	return n;
}

// Reads an input from at most left bytes. Returns the bytes used, 0 if it
// doesn't fit or makes no sense.
static uint8_t SEC_NET in_get(const uint8_t *b, uint8_t left, LsInput *p)
{
	uint8_t n = 1, i, h;

	if (left < 2)
		return 0;
	h = b[0];
	if (h & 0xC0)
		return 0;
	p->opts = (uint8_t)(h & 3);
	p->build = (uint8_t)((h >> 2) & 7);
	if (p->build > BUILD_MINE)
		return 0;
	if (h & 0x20) {
		for (i = 0; i < LS_TICKS; i++)
			p->keys[i] = b[1];
		n = 2;
	} else {
		if (left < 1 + LS_TICKS)
			return 0;
		for (i = 0; i < LS_TICKS; i++)
			p->keys[i] = b[n++];
	}
	if (p->build) {
		if (left < n + 2)
			return 0;
		p->buildX = b[n++];
		p->buildY = b[n++];
	} else {
		p->buildX = p->buildY = 0;
	}
	return n;
}

// ---- setup ----

static void SEC_NET reset(Lockstep *ls, const Link *link)
{
	uint8_t i;

	net_zero(ls, sizeof(*ls));
	ls->link = *link;
	for (i = 0; i < LS_WIN; i++)
		ls->ringTurn[i] = 0xFFFFFFFFUL;
}

static void SEC_NET set_name(char *dst, const char *src)
{
	uint8_t i;

	for (i = 0; i < LS_NAME - 1 && src[i]; i++)
		dst[i] = src[i];
	for (; i < LS_NAME; i++)
		dst[i] = 0;
}

void ls_host_open(Lockstep *ls, const Link *link, const char *name)
{
	reset(ls, link);
	ls->host = 1;
	ls->slot = 0;
	ls->seated = 1;
	ls->state = LS_LOBBY;
	set_name(ls->start.names[0], name);
	set_name(ls->me, name);
}

void ls_host_lobby(Lockstep *ls, const char *mapName, uint8_t bots, uint8_t teams,
	LsMapGet get, void *ctx)
{
	uint8_t i;

	for (i = 0; i < LS_MAPNAME - 1 && mapName[i]; i++)
		ls->mapName[i] = mapName[i];
	ls->mapName[i] = 0;
	ls->lobbyBots = bots;
	ls->lobbyTeams = teams;
	ls->mapGet = get;
	ls->mapCtx = ctx;
	ls->rosterNew = 0xFE;
	net_zero(ls->setupNext, sizeof(ls->setupNext));
}

void ls_join_open(Lockstep *ls, const Link *link, const char *name)
{
	reset(ls, link);
	ls->state = LS_HELLO;
	set_name(ls->me, name);
}

void ls_setup(const LsStart *st, GameSetup *setup)
{
	uint8_t i;

	net_zero(setup, sizeof(*setup));
	setup->gameType = st->gameType;
	setup->hiddenMines = st->hiddenMines;
	setup->numPlayers = st->numPlayers;
	setup->numBots = st->numBots;
	setup->botLevel = st->botLevel;
	setup->timeLimit = st->timeLimit;
	for (i = 0; i < LS_SLOTS; i++)
		setup->allies[i] = st->allies[i];
}

// Both sides: the game begins. Turns before the delay have no inputs.
static void SEC_NET begin(Lockstep *ls)
{
	uint8_t i;
	uint32_t t;

	ls->state = LS_PLAYING;
	ls->humans = (uint8_t)((1 << ls->start.numPlayers) - 1);
	ls->turn = 0;
	ls->sub = 0;
	ls->bundled = ls->start.delay;
	for (t = 0; t < ls->start.delay; t++) {
		uint8_t e = claim(ls, t);

		ls->act[e] = ls->humans;
		ls->have[e] = ls->humans;
		ls->done[e] = 1;
	}
	for (i = 0; i < LS_SLOTS; i++) {
		ls->ackNext[i] = ls->sentNext[i] = ls->start.delay;
		ls->ackTime[i] = ls->now;
		ls->heard[i] = ls->now;
	}
}

void ls_host_start(Lockstep *ls, const LsStart *start)
{
	char names[LS_SLOTS][LS_NAME];
	uint8_t i;

	net_copy(names, ls->start.names, sizeof(names));
	net_copy(&ls->start, start, sizeof(*start));
	net_copy(ls->start.names, names, sizeof(names));
	ls->start.numPlayers = bits(ls->seated);
	if (ls->start.delay < 1)
		ls->start.delay = 1;
	if (ls->start.delay > LS_MAX_DELAY)
		ls->start.delay = LS_MAX_DELAY;
	if (!ls->start.sumTurns)
		ls->start.sumTurns = LS_SUM_TURNS;
	if (!ls->start.dropSecs)
		ls->start.dropSecs = LS_DROP_MS / 1000;
	if (!ls->start.botLevel)
		ls->start.botLevel = BOT_NORMAL;
	ls->ready = 1;
	ls->state = LS_STARTING;
	net_zero(ls->setupNext, sizeof(ls->setupNext));
	for (i = 0; i < LS_SLOTS; i++) {
		ls->heard[i] = ls->now;
		ls->lastSend[i] = ls->now - 100000UL;     // START goes out at the next poll
	}
	if (ls->ready == ls->seated)
		begin(ls);
}

void ls_join_ready(Lockstep *ls)
{
	if (ls->state != LS_STARTING)
		return;
	begin(ls);
	ls->lastSend[0] = ls->now - 100000UL;     // READY goes out at the next poll
	ls->sentNext[0] = ls->start.delay;
	ls->gotStart = 2;   // READY to send
}

void ls_join_fetch(Lockstep *ls, LsMapPut put, void *ctx)
{
	if (ls->state != LS_STARTING || ls->fetch != LS_MAP_NONE)
		return;
	ls->mapPut = put;
	ls->mapCtx = ctx;
	ls->fetch = LS_MAP_GETTING;
	ls->mapHave = 0;
	ls->mapEarly = 0;
	ls->mapCrc = 0xFFFFFFFFUL;
	ls->mapAskAt = ls->now - 100000UL;     // MAPGET goes out at the next poll
	if (ls->start.mapLen == 0)
		ls->fetch = LS_MAP_GOT;
}

// ---- checksums ----

uint32_t ls_checksum(const GameState *g)
{
	uint32_t crc = 0xFFFFFFFFUL;
	uint16_t y0 = (uint16_t)(((g->tick >> 8) & 15) * 16), y;

	for (y = y0; y < y0 + 16; y++)
		crc = util_crc(crc, mapstore_row((uint8_t)y), 256);
	crc = util_crc(crc, mapstore_flags(MS_KNOWN) + y0 * 32, 16 * 32);
	return ~crc ^ game_checksum(g);
}

static void SEC_NET stop(Lockstep *ls, uint8_t why)
{
	if (ls->state == LS_STOPPED)
		return;
	ls->state = LS_STOPPED;
	ls->stop = why;
	ls->stopLeft = STOP_SENDS;
	ls->stopPending = ls->host ? (uint8_t)(ls->seated & ~1) : 1;
}

static void SEC_NET desync(Lockstep *ls, uint8_t slot, uint32_t tick, uint32_t local, uint32_t remote)
{
	if (ls->state == LS_STOPPED)
		return;
	ls->badSlot = slot;
	ls->badTick = tick;
	ls->badLocal = local;
	ls->badRemote = remote;
	stop(ls, LS_STOP_DESYNC);
}

// Another device's checksum for a tick: compared now if ours is made,
// kept until it is if not.
static void SEC_NET remote_sum(Lockstep *ls, uint8_t slot, uint32_t tick, uint32_t val)
{
	uint8_t i;

	// A checksum sent again over a lossy link is compared once.
	if (ls->remTick[slot] == tick && !((ls->remWait >> slot) & 1))
		return;
	ls->remTick[slot] = tick;
	for (i = 0; i < 4; i++) {
		if (ls->sumVal[i] == 0 && ls->sumTick[i] == 0)
			continue;
		if (ls->sumTick[i] == tick) {
			if (ls->sumVal[i] != val)
				desync(ls, slot, tick, ls->sumVal[i], val);
			else
				ls->sumsCompared++;
			return;
		}
	}
	if (tick > ls->sumTick[(ls->sumPos + 3) & 3]) {
		ls->remVal[slot] = val;
		ls->remWait = (uint8_t)(ls->remWait | (1 << slot));
	}
}

static void SEC_NET local_sum(Lockstep *ls, uint32_t tick, uint32_t val)
{
	uint8_t i;

	ls->sumTick[ls->sumPos] = tick;
	ls->sumVal[ls->sumPos] = val;
	ls->sumPos = (uint8_t)((ls->sumPos + 1) & 3);
	for (i = 0; i < LS_SLOTS; i++) {
		if (!((ls->remWait >> i) & 1) || ls->remTick[i] > tick)
			continue;
		ls->remWait = (uint8_t)(ls->remWait & ~(1 << i));
		if (ls->remTick[i] != tick)
			continue;
		if (ls->remVal[i] != val)
			desync(ls, i, tick, val, ls->remVal[i]);
		else
			ls->sumsCompared++;
	}
	for (i = 0; i < LS_SLOTS; i++)
		ls->sumLeft[i] = (uint8_t)(ls->link.reliable ? 1 : 8);
}

// ---- running turns ----

uint8_t ls_ready(Lockstep *ls)
{
	uint8_t e, ok;

	ok = ls->state == LS_PLAYING && (e = entry(ls, ls->turn)) != 255 && ls->done[e];
	if (ok) {
		ls->stalled = 0;
	} else if (!ls->stalled) {
		ls->stalled = 1;
		ls->stallSince = ls->now;
	}
	return ok;
}

void ls_tick(Lockstep *ls, GameState *g, const GameInput *local)
{
	GameInput all[GAME_MAX_TANKS];
	uint8_t e = entry(ls, ls->turn), i, sub = ls->sub;
	LsInput *m = &ls->mine;

	if (e == 255)
		return;
	net_zero(all, sizeof(all));
	for (i = 0; i < LS_SLOTS; i++) {
		if (sub == 0 && ((ls->handover[e] >> i) & 1))
			game_set_bot(g, i, ls->start.botLevel);
		if ((ls->act[e] >> i) & 1) {
			const LsInput *p = &ls->in[e][i];

			all[i].keys = p->keys[sub];
			all[i].opts = p->opts;
			if (sub == 0) {
				all[i].build = p->build;
				all[i].buildX = p->buildX;
				all[i].buildY = p->buildY;
			}
		}
	}
	game_tick(g, all);

	// This player's keys, for the turn the delay ahead. The last builder
	// order in a turn wins.
	m->keys[sub] = local->keys;
	m->opts = local->opts;
	if (local->build != BUILD_NONE && local->build <= BUILD_MINE) {
		m->build = local->build;
		m->buildX = local->buildX;
		m->buildY = local->buildY;
	}
	if (++ls->sub < LS_TICKS)
		return;
	ls->sub = 0;
	{
		uint32_t t = ls->turn + ls->start.delay;
		uint8_t f = claim(ls, t);

		ls->in[f][ls->slot] = *m;
		ls->have[f] = (uint8_t)(ls->have[f] | (1 << ls->slot));
		net_zero(m, sizeof(*m));
	}
	ls->turn++;
	if (ls->turn % ls->start.sumTurns == 0)
		local_sum(ls, g->tick, ls_checksum(g));
}

// Host: puts together every turn whose inputs are all in. A player who
// left, or who has been silent past the drop time while their input is
// missing, goes to a bot at the turn being put together.
static void SEC_NET bundle(Lockstep *ls)
{
	uint32_t drop = (uint32_t)ls->start.dropSecs * 1000;

	while (ls->bundled < ls->turn + HALF) {
		uint8_t e = entry(ls, ls->bundled), gone, need, i;

		gone = (uint8_t)(ls->humans & ls->leaving);
		for (i = 1; i < LS_SLOTS; i++)
			if (((ls->humans >> i) & 1) && (e == 255 || !((ls->have[e] >> i) & 1)) &&
			    ls->now - ls->heard[i] >= drop)
				gone = (uint8_t)(gone | (1 << i));
		need = (uint8_t)(ls->humans & ~gone);
		if (e == 255 || (ls->have[e] & need) != need)
			return;
		ls->act[e] = need;
		ls->handover[e] = gone;
		ls->done[e] = 1;
		ls->humans = need;
		ls->bundled++;
	}
}

// ---- receiving ----

static uint8_t SEC_NET slot_of(const Lockstep *ls, uint8_t node)
{
	uint8_t i;

	for (i = 1; i < LS_SLOTS; i++)
		if (((ls->seated >> i) & 1) && ls->node[i] == node)
			return i;
	return 255;
}

static uint8_t SEC_NET send(Lockstep *ls, uint8_t peer, const uint8_t *b, uint8_t len)
{
	uint8_t node = ls->host ? ls->node[peer] : LINK_HOST;

	if (link_send(&ls->link, node, b, len) != 0)
		return 0;
	ls->lastSend[peer] = ls->now;
	return 1;
}

static uint8_t SEC_NET send_stop(Lockstep *ls, uint8_t node, uint8_t why)
{
	uint8_t b[15];

	net_zero(b, sizeof(b));
	b[0] = MSG_STOP;
	b[1] = why;
	return link_send(&ls->link, node, b, sizeof(b)) == 0;
}

static void SEC_NET got_hello(Lockstep *ls, uint8_t node, const uint8_t *b, uint8_t len)
{
	uint8_t s, i;
	char name[LS_NAME];

	s = slot_of(ls, node);
	if (s != 255) {
		ls->heard[s] = ls->now;
		return;
	}
	if (len < 2 || b[1] != LS_VERSION) {
		send_stop(ls, node, LS_STOP_VERSION);
		return;
	}
	if (ls->state != LS_LOBBY || ls->seated == 0xFF) {
		send_stop(ls, node, LS_STOP_FULL);
		return;
	}
	for (s = 1; (ls->seated >> s) & 1; s++)
		;
	for (i = 0; i < LS_NAME - 1 && i + 2 < len; i++)
		name[i] = (char)b[i + 2];
	name[i] = 0;
	set_name(ls->start.names[s], name);
	ls->seated = (uint8_t)(ls->seated | (1 << s));
	ls->node[s] = node;
	ls->heard[s] = ls->now;
	ls->pingAt[s] = ls->now;
	ls->rtt[s] = 0;
	ls->rosterNew = 0xFE;
	net_zero(ls->setupNext, sizeof(ls->setupNext));
}

// Host, in the lobby: a joiner who left or went silent loses their seat
// and those after them move up a slot, so players stay in slots 0 up.
static void SEC_NET unseat(Lockstep *ls, uint8_t s)
{
	uint8_t i;

	for (i = s; i + 1 < LS_SLOTS; i++) {
		ls->node[i] = ls->node[i + 1];
		ls->heard[i] = ls->heard[i + 1];
		ls->pingAt[i] = ls->pingAt[i + 1];
		ls->rtt[i] = ls->rtt[i + 1];
		net_copy(ls->start.names[i], ls->start.names[i + 1], LS_NAME);
	}
	net_zero(ls->start.names[LS_SLOTS - 1], LS_NAME);
	ls->rtt[LS_SLOTS - 1] = 0;
	ls->seated = (uint8_t)((1 << (bits(ls->seated) - 1)) - 1);
	ls->rosterNew = 0xFE;
	net_zero(ls->setupNext, sizeof(ls->setupNext));
}

static void SEC_NET got_start(Lockstep *ls, const uint8_t *b, uint8_t len)
{
	LsStart *st = &ls->start;
	uint8_t i;

	if (len < START_LEN)
		return;
	if (ls->state != LS_HELLO) {
		// Our READY was lost: send it again.
		if (ls->state == LS_PLAYING && ls->gotStart)
			ls->gotStart = 2;
		return;
	}
	// START comes again until we're READY; the first one counts.
	if (ls->gotStart)
		return;
	if (b[1] != LS_VERSION) {
		stop(ls, LS_STOP_VERSION);
		ls->stopLeft = 0;
		return;
	}
	ls->slot = b[2];
	st->numPlayers = b[3];
	st->seed = net_get32(b + 4);
	st->mapSum = net_get32(b + 8);
	st->mapLen = net_get16(b + 12);
	st->gameType = b[14];
	st->hiddenMines = b[15];
	st->timeLimit = b[16];
	st->numBots = b[17];
	st->botLevel = b[18];
	st->delay = b[19];
	st->sumTurns = b[20];
	st->dropSecs = b[21];
	for (i = 0; i < LS_SLOTS; i++)
		st->allies[i] = b[22 + i];
	if (ls->slot >= LS_SLOTS || st->numPlayers > LS_SLOTS || ls->slot >= st->numPlayers ||
	    st->delay < 1 || st->delay > LS_MAX_DELAY || !st->sumTurns || !st->dropSecs) {
		stop(ls, LS_STOP_VERSION);
		ls->stopLeft = 0;
		return;
	}
	// The map's name follows, for a joiner that missed SEATS.
	if (len > START_LEN) {
		for (i = 0; i < LS_MAPNAME - 1 && START_LEN + i < len; i++)
			ls->mapName[i] = (char)b[START_LEN + i];
		ls->mapName[i] = 0;
	}
	net_zero(st->names, sizeof(st->names));
	set_name(st->names[ls->slot], ls->me);
	ls->ready = (uint8_t)(1 << ls->slot);
	ls->gotStart = 1;
	if (ls->ready == (uint8_t)((1 << st->numPlayers) - 1))
		ls->state = LS_STARTING;
}

static void SEC_NET got_name(Lockstep *ls, const uint8_t *b, uint8_t len)
{
	char name[LS_NAME];
	uint8_t s, i, atStart;

	if (len < 2 || ls->state != LS_HELLO || (b[1] & ~NAME_START) >= LS_SLOTS)
		return;
	s = (uint8_t)(b[1] & ~NAME_START);
	atStart = (uint8_t)(b[1] & NAME_START);
	for (i = 0; i < LS_NAME - 1 && i + 2 < len; i++)
		name[i] = (char)b[i + 2];
	name[i] = 0;
	// Lobby names count before START, START's names after it; one that
	// comes late or early is dropped, the host sends them again.
	if (!ls->gotStart) {
		if (!atStart)
			set_name(ls->start.names[s], name);
		return;
	}
	if (!atStart || s >= ls->start.numPlayers)
		return;
	if (s != ls->slot)
		set_name(ls->start.names[s], name);
	ls->ready = (uint8_t)(ls->ready | (1 << s));
	if (ls->ready == (uint8_t)((1 << ls->start.numPlayers) - 1))
		ls->state = LS_STARTING;
}

// A checksum trailer: the tick, then the checksum.
static void SEC_NET got_sum(Lockstep *ls, uint8_t slot, const uint8_t *b)
{
	remote_sum(ls, slot, net_get32(b), net_get32(b + 4));
}

static void SEC_NET moved_ack(Lockstep *ls, uint8_t peer, uint16_t w)
{
	uint32_t t;

	if (widen(ls, w, &t) && t > ls->ackNext[peer]) {
		ls->ackNext[peer] = t;
		ls->ackTime[peer] = ls->now;
	}
}

// Host: a joiner's inputs.
static void SEC_NET got_input(Lockstep *ls, uint8_t s, const uint8_t *b, uint8_t len)
{
	uint8_t n, pos = 7, j, used;
	uint32_t t, first;
	LsInput p;

	if (len < 7 || b[1] != s || ls->state != LS_PLAYING)
		return;
	moved_ack(ls, s, net_get16(b + 2));
	n = (uint8_t)(b[6] & F_COUNT);
	if (!widen(ls, net_get16(b + 4), &first))
		first = 0xFFFFFFFFUL;
	for (j = 0; j < n; j++) {
		used = in_get(b + pos, (uint8_t)(len - pos), &p);
		if (!used)
			return;
		pos = (uint8_t)(pos + used);
		t = first + j;
		if (first == 0xFFFFFFFFUL || t < ls->bundled || t >= ls->turn + HALF ||
		    !((ls->humans >> s) & 1))
			continue;
		{
			uint8_t e = claim(ls, t);

			ls->in[e][s] = p;
			ls->have[e] = (uint8_t)(ls->have[e] | (1 << s));
		}
	}
	if ((b[6] & F_SUM) && pos + SUM_LEN <= len)
		got_sum(ls, s, b + pos);
}

// Joiner: turns from the host.
static void SEC_NET got_turns(Lockstep *ls, const uint8_t *b, uint8_t len)
{
	uint8_t n, pos = 6, j, i, act, ho, used, e;
	uint32_t first, t;
	LsInput p[LS_SLOTS];

	if (len < 6 || ls->state != LS_PLAYING)
		return;
	moved_ack(ls, 0, net_get16(b + 1));
	n = (uint8_t)(b[5] & F_COUNT);
	if (b[5] & F_WAIT) {
		if (len < 7)
			return;
		ls->waitMask = b[pos++];
	} else {
		ls->waitMask = 0;
	}
	if (!widen(ls, net_get16(b + 3), &first))
		first = 0xFFFFFFFFUL;
	for (j = 0; j < n; j++) {
		if (pos + 2 > len)
			return;
		act = b[pos];
		ho = b[pos + 1];
		pos = (uint8_t)(pos + 2);
		for (i = 0; i < LS_SLOTS; i++) {
			if (!((act >> i) & 1))
				continue;
			used = in_get(b + pos, (uint8_t)(len - pos), &p[i]);
			if (!used)
				return;
			pos = (uint8_t)(pos + used);
		}
		t = first + j;
		if (first == 0xFFFFFFFFUL || t < ls->turn || t >= ls->turn + HALF)
			continue;
		e = claim(ls, t);
		if (ls->done[e])
			continue;
		for (i = 0; i < LS_SLOTS; i++)
			if ((act >> i) & 1)
				ls->in[e][i] = p[i];
		ls->act[e] = act;
		ls->handover[e] = ho;
		ls->done[e] = 1;
		ls->humans = act;
	}
	if ((b[5] & F_SUM) && pos + SUM_LEN <= len)
		got_sum(ls, 0, b + pos);
}

// Host: slot s leaves. In the lobby they lose their seat; once the game
// starts their tank goes to a bot.
static void SEC_NET leave_slot(Lockstep *ls, uint8_t s)
{
	if (ls->state == LS_LOBBY) {
		unseat(ls, s);
		return;
	}
	ls->leaving = (uint8_t)(ls->leaving | (1 << s));
	if (ls->state == LS_STARTING) {
		ls->ready = (uint8_t)(ls->ready | (1 << s));
		ls->fetching = (uint8_t)(ls->fetching & ~(1 << s));
		if (ls->ready == ls->seated)
			begin(ls);
	}
}

// Joiner: the lobby as the host has it.
static void SEC_NET got_seats(Lockstep *ls, const uint8_t *b, uint8_t len)
{
	uint8_t i;

	if (len < 5 || ls->state != LS_HELLO || ls->gotStart || b[1] >= LS_SLOTS)
		return;
	ls->slot = b[1];
	ls->seated = b[2];
	ls->lobbyBots = b[3];
	ls->lobbyTeams = b[4];
	for (i = 0; i < LS_MAPNAME - 1 && i + 5 < len; i++)
		ls->mapName[i] = (char)b[i + 5];
	ls->mapName[i] = 0;
	set_name(ls->start.names[ls->slot], ls->me);
	ls->inLobby = 1;
}

// Joiner: map bytes. Chunks come at multiples of LS_CHUNK; those up to
// LS_MAP_WIN - 1 ahead wait in mapBuf, the rest are dropped.
static uint8_t SEC_NET chunk_len(const Lockstep *ls, uint16_t idx)
{
	uint32_t off = (uint32_t)idx * LS_CHUNK, left = ls->start.mapLen - off;

	return (uint8_t)(left < LS_CHUNK ? left : LS_CHUNK);
}

static void SEC_NET got_mapdata(Lockstep *ls, const uint8_t *b, uint8_t len)
{
	uint16_t off, idx, have;
	uint8_t n, e;

	if (len < 4)
		return;
	if (ls->fetch == LS_MAP_GOT) {
		// The host missed that we have it all: say so again.
		ls->mapAsked = 0;
		return;
	}
	if (ls->fetch != LS_MAP_GETTING)
		return;
	off = net_get16(b + 1);
	n = (uint8_t)(len - 3);
	have = (uint16_t)(ls->mapHave / LS_CHUNK);
	if (off % LS_CHUNK || off >= ls->start.mapLen)
		return;
	idx = (uint16_t)(off / LS_CHUNK);
	if (idx < have || idx >= have + LS_MAP_WIN || n != chunk_len(ls, idx))
		return;
	e = (uint8_t)(idx % LS_MAP_WIN);
	net_copy(ls->mapBuf[e], b + 3, n);
	ls->mapEarly = (uint8_t)(ls->mapEarly | (1 << e));
	if (idx != have)
		ls->mapNack = 1;
	// Everything now in order goes to the joiner's store.
	for (;;) {
		e = (uint8_t)(have % LS_MAP_WIN);
		if (!((ls->mapEarly >> e) & 1))
			break;
		n = chunk_len(ls, have);
		ls->mapEarly = (uint8_t)(ls->mapEarly & ~(1 << e));
		if (ls->mapPut(ls->mapCtx, ls->mapHave, ls->mapBuf[e], n) != 0) {
			stop(ls, LS_STOP_MAP);
			return;
		}
		ls->mapCrc = util_crc(ls->mapCrc, ls->mapBuf[e], n);
		ls->mapHave = (uint16_t)(ls->mapHave + n);
		have++;
		if (ls->mapHave >= ls->start.mapLen) {
			if (~ls->mapCrc != ls->start.mapSum) {
				stop(ls, LS_STOP_MAP);
				return;
			}
			ls->fetch = LS_MAP_GOT;
			ls->mapAskAt = ls->now - 100000UL;    // tell the host at once
			return;
		}
	}
}

static uint8_t SEC_NET send_chunk(Lockstep *ls, uint8_t s, uint16_t off)
{
	uint8_t b[LINK_FRAME_MAX], n;
	uint16_t len = ls->start.mapLen;

	n = (uint8_t)(len - off < LS_CHUNK ? len - off : LS_CHUNK);
	b[0] = MSG_MAPDATA;
	net_put16(b + 1, off);
	ls->mapGet(ls->mapCtx, off, b + 3, n);
	if (link_send(&ls->link, ls->node[s], b, (uint8_t)(3 + n)) != 0)
		return 0;
	ls->lastSend[s] = ls->now;
	return n;
}

// Host: a joiner asks for the map from byte have on. When later chunks
// came, those missing in between are sent again at once, at most once a
// round trip.
static void SEC_NET got_mapget(Lockstep *ls, uint8_t s, const uint8_t *b, uint8_t len)
{
	uint16_t have, off;

	if (len < 3 || ls->state != LS_STARTING || ((ls->ready >> s) & 1) || !ls->mapGet)
		return;
	have = net_get16(b + 1);
	if (have > ls->start.mapLen)
		return;
	if (have == ls->start.mapLen) {
		// They have it all: START again until they are READY.
		ls->fetching = (uint8_t)(ls->fetching & ~(1 << s));
		return;
	}
	if (!((ls->fetching >> s) & 1)) {
		ls->fetching = (uint8_t)(ls->fetching | (1 << s));
		ls->mapAck[s] = ls->mapSent[s] = 0;
		ls->mapAckAt[s] = ls->now;
	}
	if (have > ls->mapAck[s]) {
		ls->mapAck[s] = have;
		ls->mapAckAt[s] = ls->now;
	}
	if (ls->mapSent[s] < ls->mapAck[s])
		ls->mapSent[s] = ls->mapAck[s];
	if (len >= 4 && b[3] && ls->now - ls->mapFast[s] >= (ls->rtt[s] > 60 ? ls->rtt[s] : 60)) {
		for (off = have; off < ls->mapSent[s]; off = (uint16_t)(off + LS_CHUNK))
			if (!((b[3] >> (off / LS_CHUNK % LS_MAP_WIN)) & 1)) {
				if (!send_chunk(ls, s, off))
					break;
				ls->mapFast[s] = ls->now;
			}
	}
}

static void SEC_NET got_stop(Lockstep *ls, uint8_t s, const uint8_t *b, uint8_t len)
{
	if (len < 15 || ls->state == LS_STOPPED)
		return;
	if (b[1] == LS_STOP_DESYNC) {
		// Theirs and ours swap over.
		desync(ls, ls->host ? s : b[2], net_get32(b + 3), net_get32(b + 11), net_get32(b + 7));
		return;
	}
	if (!ls->host) {
		if (b[1] == LS_STOP_END && ls->state == LS_PLAYING) {
			// The host's game is over; ours runs on to the same end with
			// the turns it has.
			ls->ended = 1;
			ls->heard[0] = ls->now;
			return;
		}
		stop(ls, b[1] == LS_STOP_END ? LS_STOP_HOST_LEFT : b[1]);
		ls->stopLeft = 0;
	} else {
		// A joiner that gave up leaves.
		leave_slot(ls, s);
	}
}

static void SEC_NET receive(Lockstep *ls)
{
	uint8_t b[LINK_FRAME_MAX], len, node, s;

	while ((len = link_recv(&ls->link, &node, b)) != 0) {
		if (ls->host) {
			if (b[0] == MSG_HELLO) {
				got_hello(ls, node, b, len);
				continue;
			}
			s = slot_of(ls, node);
			if (s == 255)
				continue;
			if (ls->state == LS_PLAYING && !((ls->humans >> s) & 1)) {
				// Their tank is a bot's now: they hear only that, so they
				// stop rather than wait on turns that never come.
				if (b[0] != MSG_STOP && b[0] != MSG_LEAVE && ls->now - ls->lastSend[s] >= 200) {
					if (send_stop(ls, node, LS_STOP_DROPPED))
						ls->lastSend[s] = ls->now;
				}
				continue;
			}
		} else {
			if (node != LINK_HOST)
				continue;
			s = 0;
		}
		ls->heard[s] = ls->now;
		switch (b[0]) {
		case MSG_START:
			if (!ls->host)
				got_start(ls, b, len);
			break;
		case MSG_NAME:
			if (!ls->host)
				got_name(ls, b, len);
			break;
		case MSG_READY:
			if (ls->host && len >= 2 && b[1] == s && ls->state == LS_STARTING) {
				ls->ready = (uint8_t)(ls->ready | (1 << s));
				ls->fetching = (uint8_t)(ls->fetching & ~(1 << s));
				if (ls->ready == ls->seated)
					begin(ls);
			}
			break;
		case MSG_INPUT:
			if (ls->host)
				got_input(ls, s, b, len);
			break;
		case MSG_TURNS:
			if (!ls->host)
				got_turns(ls, b, len);
			break;
		case MSG_PING:
			if (len >= 5) {
				b[0] = MSG_PONG;
				send(ls, s, b, 5);
			}
			break;
		case MSG_PONG:
			if (len >= 5) {
				uint32_t r = ls->now - net_get32(b + 1);

				if (r > 60000UL)
					break;
				if (r == 0)
					r = 1;
				ls->rtt[s] = (uint16_t)(ls->rtt[s] ? (ls->rtt[s] * 7UL + r) / 8 : r);
			}
			break;
		case MSG_STOP:
			got_stop(ls, s, b, len);
			break;
		case MSG_LEAVE:
			if (ls->host && len >= 2 && b[1] == s)
				leave_slot(ls, s);
			break;
		case MSG_SEATS:
			if (!ls->host)
				got_seats(ls, b, len);
			break;
		case MSG_MAPGET:
			if (ls->host)
				got_mapget(ls, s, b, len);
			break;
		case MSG_MAPDATA:
			if (!ls->host)
				got_mapdata(ls, b, len);
			break;
		}
	}
}

// ---- sending ----

static uint16_t SEC_NET resend_ms(const Lockstep *ls, uint8_t peer)
{
	uint16_t r = ls->rtt[peer];

	return (uint16_t)(r ? (r < 30 ? 100 : 2 * r + 40) : 200);
}

// Where the next frame to a peer starts, and whether one is due. Over a
// reliable link each turn goes once, and again from what they have only
// when nothing new is acknowledged for a while. Over a lossy link every
// frame carries all they haven't acknowledged, sent when there is
// something new or LOSSY_MS after the last.
#define LOSSY_MS 60
static uint8_t SEC_NET due(Lockstep *ls, uint8_t peer, uint32_t avail, uint32_t *first)
{
	uint8_t fresh;

	*first = ls->sentNext[peer] < ls->ackNext[peer] ? ls->ackNext[peer] : ls->sentNext[peer];
	fresh = *first < avail;
	if (!ls->link.reliable) {
		*first = ls->ackNext[peer];
		return (uint8_t)(fresh || (ls->ackNext[peer] < avail && ls->now - ls->lastSend[peer] >= LOSSY_MS) ||
			ls->sumLeft[peer] || ls->now - ls->lastSend[peer] >= KEEPALIVE_MS);
	}
	if (ls->ackNext[peer] < ls->sentNext[peer] &&
	    ls->now - ls->ackTime[peer] >= resend_ms(ls, peer)) {
		*first = ls->ackNext[peer];
		fresh = 1;
	}
	return (uint8_t)(fresh || ls->sumLeft[peer] || ls->now - ls->lastSend[peer] >= KEEPALIVE_MS);
}

// The first turn from t on that lacks what we wait for from peer: the
// joiner's input (host), or a turn (joiner).
static uint32_t SEC_NET next_needed(const Lockstep *ls, uint8_t peer)
{
	uint32_t t = ls->host ? ls->bundled : ls->turn;
	uint8_t e;

	for (; t < ls->turn + HALF; t++) {
		e = entry(ls, t);
		if (e == 255 || (ls->host ? !((ls->have[e] >> peer) & 1) : !ls->done[e]))
			break;
	}
	return t;
}

static uint8_t SEC_NET put_sum(Lockstep *ls, uint8_t peer, uint8_t *b, uint8_t *pos)
{
	uint8_t last = (uint8_t)((ls->sumPos + 3) & 3);

	if (!ls->sumLeft[peer] || *pos + SUM_LEN > LINK_FRAME_MAX)
		return 0;
	net_put32(b + *pos, ls->sumTick[last]);
	net_put32(b + *pos + 4, ls->sumVal[last]);
	*pos = (uint8_t)(*pos + SUM_LEN);
	return F_SUM;
}

// Joiner: our inputs not yet acknowledged.
static void SEC_NET send_inputs(Lockstep *ls)
{
	uint8_t b[LINK_FRAME_MAX], pos = 7, n = 0, e;
	uint32_t t, end = ls->turn + ls->start.delay, first;

	if (!due(ls, 0, end, &first))
		return;
	for (t = first; t < end && n < F_COUNT; t++, n++) {
		e = entry(ls, t);
		if (e == 255 || !((ls->have[e] >> ls->slot) & 1) ||
		    pos + in_size(&ls->in[e][ls->slot]) > LINK_FRAME_MAX)
			break;
		pos = (uint8_t)(pos + in_put(b + pos, &ls->in[e][ls->slot]));
	}
	b[0] = MSG_INPUT;
	b[1] = ls->slot;
	net_put16(b + 2, (uint16_t)next_needed(ls, 0));
	net_put16(b + 4, (uint16_t)first);
	b[6] = (uint8_t)(n | put_sum(ls, 0, b, &pos));
	if (!send(ls, 0, b, pos))
		return;
	if (b[6] & F_SUM)
		ls->sumLeft[0]--;
	if (ls->link.reliable && first < ls->sentNext[0])
		ls->ackTime[0] = ls->now;
	ls->sentNext[0] = first + n;
	// Lossy: what didn't fit waits for LOSSY_MS or something new.
	if (!ls->link.reliable)
		ls->sentNext[0] = end;
}

// Host: turns put together that joiner s doesn't have.
static void SEC_NET send_turns(Lockstep *ls, uint8_t s)
{
	uint8_t b[LINK_FRAME_MAX], pos = 6, n = 0, e, i, size, wait = 0;
	uint32_t t, first;

	if (!due(ls, s, ls->bundled, &first))
		return;
	if (ls->stalled) {
		e = entry(ls, ls->turn);
		wait = (uint8_t)(ls->humans & ~(e == 255 ? 1 : ls->have[e]));
		if (wait)
			b[pos++] = wait;
	}
	for (t = first; t < ls->bundled && n < F_COUNT; t++, n++) {
		e = entry(ls, t);
		if (e == 255)
			break;
		size = 2;
		for (i = 0; i < LS_SLOTS; i++)
			if ((ls->act[e] >> i) & 1)
				size = (uint8_t)(size + in_size(&ls->in[e][i]));
		if (pos + size > LINK_FRAME_MAX)
			break;
		b[pos++] = ls->act[e];
		b[pos++] = ls->handover[e];
		for (i = 0; i < LS_SLOTS; i++)
			if ((ls->act[e] >> i) & 1)
				pos = (uint8_t)(pos + in_put(b + pos, &ls->in[e][i]));
	}
	b[0] = MSG_TURNS;
	net_put16(b + 1, (uint16_t)next_needed(ls, s));
	net_put16(b + 3, (uint16_t)first);
	b[5] = (uint8_t)(n | (wait ? F_WAIT : 0) | put_sum(ls, s, b, &pos));
	if (!send(ls, s, b, pos))
		return;
	if (b[5] & F_SUM)
		ls->sumLeft[s]--;
	if (ls->link.reliable && first < ls->sentNext[s])
		ls->ackTime[s] = ls->now;
	ls->sentNext[s] = first + n;
	if (!ls->link.reliable)
		ls->sentNext[s] = ls->bundled;
}

// Host: START and every name, to a joiner not ready yet.
static void SEC_NET send_start(Lockstep *ls, uint8_t s)
{
	const LsStart *st = &ls->start;
	uint8_t b[LINK_FRAME_MAX], i, j;

	b[0] = MSG_START;
	b[1] = LS_VERSION;
	b[2] = s;
	b[3] = st->numPlayers;
	net_put32(b + 4, st->seed);
	net_put32(b + 8, st->mapSum);
	net_put16(b + 12, st->mapLen);
	b[14] = st->gameType;
	b[15] = st->hiddenMines;
	b[16] = st->timeLimit;
	b[17] = st->numBots;
	b[18] = st->botLevel;
	b[19] = st->delay;
	b[20] = st->sumTurns;
	b[21] = st->dropSecs;
	for (i = 0; i < LS_SLOTS; i++)
		b[22 + i] = st->allies[i];
	for (j = 0; j < LS_MAPNAME - 1 && ls->mapName[j]; j++)
		b[START_LEN + j] = (uint8_t)ls->mapName[j];
	if (!ls->setupNext[s]) {
		if (!send(ls, s, b, (uint8_t)(START_LEN + j)))
			return;
		ls->setupNext[s] = 1;
	}
	for (i = (uint8_t)(ls->setupNext[s] - 1); i < st->numPlayers; i++) {
		b[0] = MSG_NAME;
		b[1] = (uint8_t)(i | NAME_START);
		for (j = 0; j < LS_NAME - 1 && st->names[i][j]; j++)
			b[2 + j] = (uint8_t)st->names[i][j];
		if (!send(ls, s, b, (uint8_t)(2 + j)))
			return;
		ls->setupNext[s] = (uint8_t)(i + 2);
	}
	ls->setupNext[s] = 0;
}

// Host: the lobby to joiner s, their slot, who is seated and their names.
static uint8_t SEC_NET send_roster(Lockstep *ls, uint8_t s)
{
	uint8_t b[LINK_FRAME_MAX], i, j;

	b[0] = MSG_SEATS;
	b[1] = s;
	b[2] = ls->seated;
	b[3] = ls->lobbyBots;
	b[4] = ls->lobbyTeams;
	for (j = 0; j < LS_MAPNAME - 1 && ls->mapName[j]; j++)
		b[5 + j] = (uint8_t)ls->mapName[j];
	if (!ls->setupNext[s]) {
		if (!send(ls, s, b, (uint8_t)(5 + j)))
			return 0;
		ls->setupNext[s] = 1;
	}
	for (i = (uint8_t)(ls->setupNext[s] - 1); i < LS_SLOTS; i++) {
		if (!((ls->seated >> i) & 1) || i == s)
			continue;
		b[0] = MSG_NAME;
		b[1] = i;
		for (j = 0; j < LS_NAME - 1 && ls->start.names[i][j]; j++)
			b[2 + j] = (uint8_t)ls->start.names[i][j];
		if (!send(ls, s, b, (uint8_t)(2 + j)))
			return 0;
		ls->setupNext[s] = (uint8_t)(i + 2);
	}
	ls->setupNext[s] = 0;
	return 1;
}

// Host: map chunks to joiner s, up to LS_MAP_WIN ahead of what they have.
// When what they have doesn't move for a while, from there again.
static void SEC_NET send_map(Lockstep *ls, uint8_t s)
{
	uint8_t n, retry;
	uint16_t off = ls->mapSent[s], len = ls->start.mapLen;
	uint32_t wait = ls->link.reliable ? 2000 : (uint32_t)resend_ms(ls, s) + 100;

	retry = off > ls->mapAck[s] && ls->now - ls->mapAckAt[s] >= wait;
	if (retry)
		off = ls->mapAck[s];
	while (off < len && (uint32_t)off < (uint32_t)ls->mapAck[s] + LS_MAP_WIN * LS_CHUNK) {
		n = send_chunk(ls, s, off);
		if (!n)
			break;
		if (retry) {
			ls->mapAckAt[s] = ls->now;
			retry = 0;
		}
		off = (uint16_t)(off + n);
		ls->mapSent[s] = off;
	}
}

static void SEC_NET send_stops(Lockstep *ls)
{
	uint8_t b[15], i;

	b[0] = MSG_STOP;
	b[1] = ls->stop;
	b[2] = ls->badSlot;
	net_put32(b + 3, ls->badTick);
	net_put32(b + 7, ls->badLocal);
	net_put32(b + 11, ls->badRemote);
	if (ls->host) {
		for (i = 1; i < LS_SLOTS; i++)
			if (((ls->stopPending >> i) & 1) && send(ls, i, b, sizeof(b)))
				ls->stopPending = (uint8_t)(ls->stopPending & ~(1 << i));
	} else if (ls->stop == LS_STOP_NONE) {
		// We left: LEAVE rather than STOP.
		b[0] = MSG_LEAVE;
		b[1] = ls->slot;
		if (send(ls, 0, b, 2))
			ls->stopPending = 0;
	} else {
		if (send(ls, 0, b, sizeof(b)))
			ls->stopPending = 0;
	}
	if (!ls->stopPending) {
		ls->stopLeft--;
		ls->stopPending = ls->host ? (uint8_t)(ls->seated & ~1) : 1;
	}
}

static void SEC_NET send_ping(Lockstep *ls, uint8_t peer)
{
	uint8_t b[5];

	if (ls->now - ls->pingAt[peer] < PING_MS)
		return;
	b[0] = MSG_PING;
	net_put32(b + 1, ls->now);
	if (send(ls, peer, b, 5))
		ls->pingAt[peer] = ls->now;
}

void ls_poll(Lockstep *ls, uint32_t now)
{
	uint8_t i;

	ls->now = now;
	if (ls->state == LS_IDLE)
		return;
	receive(ls);
	if (ls->state == LS_STOPPED) {
		if (ls->stopLeft)
			send_stops(ls);
		return;
	}
	if (ls->host) {
		if (ls->state == LS_LOBBY) {
			// A joiner gone quiet loses their seat.
			for (i = LS_SLOTS - 1; i >= 1; i--)
				if (((ls->seated >> i) & 1) && now - ls->heard[i] >= LS_DROP_MS)
					unseat(ls, i);
			if (now - ls->rosterAt >= ROSTER_MS) {
				ls->rosterAt = now;
				ls->rosterNew = 0xFE;
			}
			for (i = 1; i < LS_SLOTS; i++)
				if (((ls->seated & ls->rosterNew) >> i) & 1)
					if (send_roster(ls, i))
						ls->rosterNew = (uint8_t)(ls->rosterNew & ~(1 << i));
		}
		if (ls->state == LS_STARTING) {
			// A joiner that never answers START plays as a bot.
			for (i = 1; i < LS_SLOTS; i++)
				if (((ls->seated >> i) & 1) && !((ls->ready >> i) & 1) &&
				    now - ls->heard[i] >= LS_START_MS) {
					ls->ready = (uint8_t)(ls->ready | (1 << i));
					ls->leaving = (uint8_t)(ls->leaving | (1 << i));
					ls->fetching = (uint8_t)(ls->fetching & ~(1 << i));
				}
			if (ls->ready == ls->seated)
				begin(ls);
		}
		if (ls->state == LS_PLAYING)
			bundle(ls);
		for (i = 1; i < LS_SLOTS; i++) {
			if (!((ls->seated >> i) & 1) ||
			    (ls->state == LS_PLAYING && !((ls->humans >> i) & 1)))
				continue;
			send_ping(ls, i);
			if (ls->state == LS_STARTING && !((ls->ready >> i) & 1)) {
				if ((ls->fetching >> i) & 1)
					send_map(ls, i);
				else if (ls->setupNext[i] || now - ls->lastSend[i] >= resend_ms(ls, i))
					send_start(ls, i);
			} else if (ls->state == LS_PLAYING && ((ls->humans >> i) & 1)) {
				send_turns(ls, i);
			}
		}
	} else {
		if (ls->state == LS_HELLO && now - ls->lastSend[0] >= HELLO_MS) {
			uint8_t b[LINK_FRAME_MAX], j;

			b[0] = MSG_HELLO;
			b[1] = LS_VERSION;
			for (j = 0; j < LS_NAME - 1 && ls->me[j]; j++)
				b[2 + j] = (uint8_t)ls->me[j];
			send(ls, 0, b, (uint8_t)(2 + j));
		}
		send_ping(ls, 0);
		// Nothing from the host for a while: in the lobby and while the
		// game starts there's more time, as loading a map takes a while.
		if ((ls->state == LS_HELLO && ls->inLobby && now - ls->heard[0] >= LS_DROP_MS) ||
		    (ls->state == LS_STARTING && now - ls->heard[0] >= LS_START_MS)) {
			stop(ls, LS_STOP_HOST_LOST);
			ls->stopLeft = 0;
			return;
		}
		if ((ls->state == LS_STARTING || ls->state == LS_PLAYING) && ls->fetch != LS_MAP_NONE) {
			uint16_t have = ls->mapHave;

			// What we have so far, when it has moved on half a window or
			// now and then while waiting.
			if (have >= ls->mapAsked + LS_MAP_WIN / 2 * LS_CHUNK ||
			    (ls->fetch == LS_MAP_GETTING && now - ls->mapAskAt >= MAPGET_MS) ||
			    (ls->mapNack && now - ls->mapAskAt >= MAPGET_MS / 4) ||
			    (ls->fetch == LS_MAP_GOT && have != ls->mapAsked)) {
				uint8_t b[4];

				b[0] = MSG_MAPGET;
				net_put16(b + 1, have);
				b[3] = ls->fetch == LS_MAP_GETTING ? ls->mapEarly : 0;
				if (send(ls, 0, b, 4)) {
					ls->mapAsked = have;
					ls->mapAskAt = now;
					ls->mapNack = 0;
				}
			}
		}
		if (ls->state == LS_PLAYING) {
			if (now - ls->heard[0] >= (uint32_t)ls->start.dropSecs * 1000) {
				stop(ls, LS_STOP_HOST_LOST);
				ls->stopLeft = 0;
				return;
			}
			if (ls->gotStart == 2) {
				uint8_t b[2];

				b[0] = MSG_READY;
				b[1] = ls->slot;
				if (send(ls, 0, b, 2))
					ls->gotStart = 1;
			}
			send_inputs(ls);
		}
	}
}

// ---- status ----

uint8_t ls_waiting(const Lockstep *ls)
{
	uint8_t e;

	if (!ls->stalled || ls->now - ls->stallSince < LS_WAIT_MS)
		return 0;
	if (ls->host) {
		if (ls->state == LS_STARTING)
			return (uint8_t)(ls->seated & ~ls->ready);
		e = entry(ls, ls->turn);
		return (uint8_t)(ls->humans & ~(e == 255 ? 1 : ls->have[e]));
	}
	return ls->waitMask ? ls->waitMask : 1;
}

uint16_t ls_rtt(const Lockstep *ls)
{
	uint16_t m = 0;
	uint8_t i;

	for (i = 0; i < LS_SLOTS; i++)
		if (ls->rtt[i] > m)
			m = ls->rtt[i];
	return m;
}

// The input for turn T + delay is made as turn T ends and must be back
// from the host before turn T + delay starts: delay turns of 80 ms for the
// round trip, plus a turn for jitter and the host's own pace.
uint8_t ls_delay_for(uint16_t rttMs)
{
	uint16_t d = (uint16_t)((rttMs + 40 + LS_TICKS * 20 - 1) / (LS_TICKS * 20) + 1);

	if (d < 2)
		d = 2;
	return (uint8_t)(d > LS_MAX_DELAY ? LS_MAX_DELAY : d);
}

void ls_leave(Lockstep *ls)
{
	if (ls->state == LS_STOPPED || ls->state == LS_IDLE)
		return;
	if (ls->host) {
		stop(ls, ls->finishing ? LS_STOP_END : LS_STOP_HOST_LEFT);
	} else {
		stop(ls, LS_STOP_NONE);
		// After the end nothing is owed: no LEAVE, so no bot takes over.
		if (ls->finishing || ls->ended)
			ls->stopLeft = 0;
	}
}

void ls_finish(Lockstep *ls, const GameState *g)
{
	if (ls->finishing || ls->state != LS_PLAYING)
		return;
	ls->finishing = 1;
	ls->finishAt = ls->now;
	ls->endTurn = (g->tick + LS_TICKS - 1) / LS_TICKS;
}

uint8_t ls_finished(const Lockstep *ls)
{
	uint8_t i;

	if (ls->state != LS_PLAYING)
		return 1;
	if (!ls->finishing)
		return 0;
	if (ls->now - ls->finishAt >= LS_END_MS)
		return 1;
	if (!ls->host)
		return ls->ended;
	for (i = 1; i < LS_SLOTS; i++)
		if (((ls->humans >> i) & 1) && ls->ackNext[i] < ls->endTurn)
			return 0;
	return 1;
}
