// Network play on the Palm: one Lockstep on one link, the lobby as the
// forms show it, getting a map the joiner lacks, and the turns of a game.
// See netgame.h, and doc/lockstep.md for the protocol.
#include "palmbolo.h"
#include "netgame.h"
#include "net/lockstep.h"
#include "net/infrared.h"
#include "net/udp.h"
#include "net/bluetooth.h"
#include "game/game.h"
#include "game/util.h"
#ifdef LOOPBACK_TEST
#include "looptest.h"
#endif

static Lockstep gLs;
static Link gLink;
static GameState *gG;
static UInt8 gPhase, gKind;
static Boolean gInPoll, gInTick;
static UInt16 gIndex;               // host: the map's place in the list
static Char gMap[kMapNameLen];      // the map's name, a joiner's once it has it
static UInt32 gSum, gLen;           // host: the map's CRC and length
static MapBytes gBytes;             // host: the map, to send
static MapIn gIn;                   // joiner: the map as it comes
static Boolean gGotMap;             // joiner: the map came over the link
static Char gWhy[48];
static UInt16 gSeq;
static UInt32 gLobbySum;
static UInt8 gBotsSeen;             // tanks known to be bots, for the "took over" line
static Char gNote[40];
static UInt32 gNoteUntil;
static UInt32 gJoinAt;
#ifdef DEBUG
static UInt32 gCompared;
static UInt8 gWaitLogged;
#endif

UInt32 SEC_NET NetNowMs(void)
{
	UInt16 tps = SysTicksPerSecond();

	return tps == 100 ? TimGetTicks() * 10 : TimGetTicks() * 1000 / tps;
}

UInt8 NetLinks(UInt8 *kinds, const Char **names)
{
	UInt8 n = 0;

#ifdef LOOPBACK_TEST
	kinds[n] = LINK_LOOP;
	names[n++] = "Loopback";
#endif
	if (ir_available()) {
		kinds[n] = LINK_IR;
		names[n++] = "Infrared";
	}
	if (udp_available()) {
		kinds[n] = LINK_UDP;
		names[n++] = "Network";
	}
	if (bt_available()) {
		kinds[n] = LINK_BT;
		names[n++] = "Bluetooth";
	}
	return n;
}

static UInt8 SEC_NET Bits(UInt8 m)
{
	UInt8 n = 0;

	for (; m; m = (UInt8)(m & (m - 1)))
		n++;
	return n;
}

static void SEC_NET Why(const Char *s)
{
	StrCopy(gWhy, s);
}

static void SEC_NET Clear(void)
{
	MemSet(&gLs, sizeof(gLs), 0);
	gPhase = NET_OFF;
	gWhy[0] = 0;
	gGotMap = false;
	gSeq++;
	gLobbySum = 0;
	gNoteUntil = 0;
#ifdef DEBUG
	gCompared = 0;
	gWaitLogged = 0;
#endif
}

static Boolean SEC_NET OpenLink(UInt8 kind, UInt8 role)
{
#ifdef LOOPBACK_TEST
	// The second copy of the game takes the other end.
	if (kind == LINK_LOOP && !LoopPeerOpen(role == LINK_ROLE_HOST ? LINK_ROLE_JOIN : LINK_ROLE_HOST))
		return false;
#endif
	return link_open(&gLink, kind, role) == 0;
}

Boolean NetHost(UInt8 kind, UInt16 index, const Char *name, GameState *g)
{
	const AppPrefs *p = AppPrefsGet();

	NetEnd();
	Clear();
	gG = g;
	gKind = kind;
	gIndex = index;
	StrCopy(gMap, name);
	if (!MapFileSum(index, name, &gSum, &gLen) || gLen > 0xFFFFUL ||
	    !MapBytesOpen(&gBytes, index, name)) {
		Why(gLen > 0xFFFFUL ? "The map is too big to send." : "Can't read the map.");
		return false;
	}
	if (!OpenLink(kind, LINK_ROLE_HOST)) {
		MapBytesClose(&gBytes);
		Why(kind == LINK_UDP ? udp_error() : kind == LINK_BT ? bt_error() : "Can't open the link.");
		return false;
	}
	ls_host_open(&gLs, &gLink, p->name);
	ls_host_lobby(&gLs, name, p->numBots, p->teams, (LsMapGet)MapBytesGet, &gBytes);
	gPhase = NET_LOBBY;
	return true;
}

Boolean NetJoin(UInt8 kind, GameState *g)
{
	NetEnd();
	Clear();
	gG = g;
	gKind = kind;
	gMap[0] = 0;
	if (!OpenLink(kind, LINK_ROLE_JOIN)) {
		Why(kind == LINK_UDP ? udp_error() : kind == LINK_BT ? bt_error() : "Can't open the link.");
		return false;
	}
	ls_join_open(&gLs, &gLink, AppPrefsGet()->name);
	gJoinAt = NetNowMs();
	gPhase = NET_LOBBY;
	return true;
}

// The tanks: players, then as many bots as fit.
static UInt8 SEC_NET Players(void)
{
	if (gLs.state >= LS_STARTING && (gLs.host || gLs.gotStart))
		return gLs.start.numPlayers;
	return Bits(gLs.seated);
}

static UInt8 SEC_NET Bots(UInt8 players)
{
	UInt8 b = gLs.state >= LS_STARTING && (gLs.host || gLs.gotStart) ? gLs.start.numBots : gLs.lobbyBots;

	return players + b > GAME_MAX_TANKS ? (UInt8)(GAME_MAX_TANKS - players) : b;
}

Boolean NetCanStart(void)
{
	return gPhase == NET_LOBBY && gLs.host && Bits(gLs.seated) >= 2;
}

Boolean NetStart(void)
{
	const AppPrefs *p = AppPrefsGet();
	LsStart st;
	GameSetup setup;
	UInt8 players, bots, n, half, i, j;
	Int16 err;

	if (!NetCanStart())
		return false;
	players = Bits(gLs.seated);
	bots = Bots(players);
	err = MapLoad(gIndex, gMap, gG);
	if (err != MAP_OK) {
		Why("Can't read the map.");
		return false;
	}
	MemSet(&st, sizeof(st), 0);
	st.seed = TimGetSeconds() ^ (TimGetTicks() << 12);
	st.mapSum = gSum;
	st.mapLen = (UInt16)gLen;
	st.gameType = p->gameType;
	st.hiddenMines = p->hiddenMines;
	st.timeLimit = (UInt8)AppTimeMinutes(p->timeLimit);
	st.numBots = bots;
	st.botLevel = (UInt8)(p->botLevel + BOT_EASY);
	st.delay = ls_delay_for(ls_rtt(&gLs));
	// Teams: the first half of the tanks against the rest.
	n = (UInt8)(players + bots);
	half = (UInt8)((n + 1) / 2);
	if (p->teams)
		for (i = 0; i < n; i++)
			for (j = 0; j < n; j++)
				if (i != j && (i < half) == (j < half))
					st.allies[i] = (UInt8)(st.allies[i] | (1 << j));
	gLs.lobbyBots = bots;
	ls_host_start(&gLs, &st);
	ls_setup(&gLs.start, &setup);
	game_init(gG, gLs.start.seed, &setup);
	gBotsSeen = gG->botMask;
#if defined(DEBUG) && defined(TEST_TIME_LIMIT)
	gG->timeLimit = TEST_TIME_LIMIT;   // ticks, the same on every Palm
#endif
	LOG(("net: start, %u players, %u bots, delay %u, rtt %u, map %lx %lu", (UInt16)players,
		(UInt16)bots, (UInt16)gLs.start.delay, ls_rtt(&gLs), gSum, gLen));
	gPhase = gLs.state == LS_PLAYING ? NET_PLAY : NET_STARTING;
	return true;
}

// A joiner gives up while the game starts.
static void SEC_NET JoinFail(const Char *why)
{
	ls_leave(&gLs);
	Why(why);
	gPhase = NET_STOPPED;
}

// Joiner: START is in. Finds the map or asks for it, and once it's here
// sets up the game and says READY.
static void SEC_NET JoinSetUp(void)
{
	GameSetup setup;
	Int16 index = 1, err;

	gPhase = NET_STARTING;
	if (gLs.fetch == LS_MAP_GETTING)
		return;
	if (gLs.fetch == LS_MAP_NONE) {
		// In the loopback test this end gets every map but Everard Island.
		index = MapFind(gLs.start.mapSum, gLs.start.mapLen, gMap, gKind != LINK_LOOP);
		if (index < 0) {
			LOG(("net: getting map %s, %u bytes", gLs.mapName, gLs.start.mapLen));
			if (!MapInOpen(&gIn, gLs.start.mapLen)) {
				JoinFail("No room for the map.");
				return;
			}
			ls_join_fetch(&gLs, (LsMapPut)MapInPut, &gIn);
			if (gLs.fetch == LS_MAP_GETTING)
				return;
		}
	}
	if (gLs.fetch == LS_MAP_GOT) {
		if (!MapInKeep(&gIn, gLs.mapName[0] ? gLs.mapName : "Network map", gMap)) {
			JoinFail("No room for the map.");
			return;
		}
		gGotMap = true;
		index = 1;
		LOG(("net: map kept as %s", gMap));
	}
	err = MapLoad((UInt16)index, gMap, gG);
	if (err != MAP_OK) {
		JoinFail("Can't read the map.");
		return;
	}
	ls_setup(&gLs.start, &setup);
	game_init(gG, gLs.start.seed, &setup);
	gBotsSeen = gG->botMask;
#if defined(DEBUG) && defined(TEST_TIME_LIMIT)
	gG->timeLimit = TEST_TIME_LIMIT;   // ticks, the same on every Palm
#endif
	ls_join_ready(&gLs);
	ls_poll(&gLs, NetNowMs());
	LOG(("net: joined as slot %u of %u, delay %u", (UInt16)gLs.slot, (UInt16)gLs.start.numPlayers,
		(UInt16)gLs.start.delay));
	gPhase = NET_PLAY;
}

const Char *NetName(UInt8 slot)
{
	return slot < LS_SLOTS ? gLs.start.names[slot] : "";
}

static void SEC_NET Stopped(void)
{
	UInt8 s = gLs.badSlot;

	gPhase = NET_STOPPED;
	switch (gLs.stop) {
	case LS_STOP_DESYNC:
		// Name the other side.
		if (s == gLs.slot)
			s = 0;
		StrPrintF(gWhy, "Out of step with %s. The game stopped.", NetName(s));
		LOG(("net: out of step at tick %lu, %lx here, %lx at slot %u", gLs.badTick, gLs.badLocal,
			gLs.badRemote, (UInt16)gLs.badSlot));
#ifdef LOOPBACK_TEST
		if (gKind == LINK_LOOP)
			LoopSaveDesync(gG, &gLs);
#endif
		break;
	case LS_STOP_HOST_LEFT: Why("The host left the game."); break;
	case LS_STOP_HOST_LOST: Why("Lost touch with the host."); break;
	case LS_STOP_VERSION: Why("The host has another version of PalmBolo."); break;
	case LS_STOP_FULL: Why("That game is full or has started."); break;
	case LS_STOP_MAP: Why("The map didn't arrive."); break;
	case LS_STOP_DROPPED: Why("Lost touch, so a bot took over your tank."); break;
	default:
		break;
	}
	LOG(("net: stopped (%u)", (UInt16)gLs.stop));
}

// Changes the count when anything the lobby shows has changed.
static void SEC_NET LobbyCheck(void)
{
	UInt8 b[24];
	UInt32 sum;
	UInt8 i;

	MemSet(b, sizeof(b), 0);
	b[0] = gPhase;
	b[1] = gLs.state;
	b[2] = gLs.seated;
	b[3] = gLs.ready;
	b[4] = gLs.fetching;
	b[5] = gLs.fetch;
	b[6] = gLs.inLobby;
	b[7] = (UInt8)(gLs.start.mapLen ? (UInt32)gLs.mapHave * 20 / gLs.start.mapLen : 0);
	for (i = 0; i < LS_SLOTS; i++)
		b[8 + i] = (UInt8)(gLs.start.mapLen ? (UInt32)gLs.mapAck[i] * 20 / gLs.start.mapLen : 0);
	sum = util_crc(0xFFFFFFFFUL, b, sizeof(b));
	sum = util_crc(sum, (const UInt8 *)gLs.start.names, sizeof(gLs.start.names));
	sum = util_crc(sum, (const UInt8 *)gLs.mapName, sizeof(gLs.mapName));
	if (gKind == LINK_BT) {
		const Char *status = bt_status();
		sum = util_crc(sum, (const UInt8 *)status, StrLen(status));
	}
	if (sum != gLobbySum) {
		gLobbySum = sum;
		gSeq++;
	}
}

// What the link says went wrong, "" while it's fine.
static const Char * SEC_NET LinkError(void)
{
	return gKind == LINK_UDP ? udp_error() : gKind == LINK_BT ? bt_error() : "";
}

// The link has failed or its session is over, so no send can get through.
static Boolean SEC_NET LinkDown(void)
{
	return LinkError()[0] || (gKind == LINK_IR && ir_dead());
}

// How long a joiner waits for a host to seat it, ms, and what it says then.
static UInt32 SEC_NET JoinWait(const Char **why)
{
	switch (gKind) {
	case LINK_UDP: *why = "No host reply. Check address and port."; return 10000;
	case LINK_BT: *why = "No reply from the Bluetooth game."; return 45000;
	case LINK_IR: *why = "No reply from the infrared game."; return 45000;
	}
	return 0;
}

static void SEC_NET TransportSync(void)
{
	UInt8 i, nodes = 0;
	for (i = 1; i < LS_SLOTS; i++)
		if (gLs.seated & (1 << i)) nodes |= 1 << gLs.node[i];
	if (gKind == LINK_UDP) udp_sync(gLs.host && gLs.state == LS_LOBBY, nodes);
	else if (gKind == LINK_BT) bt_sync(gLs.host && gLs.state == LS_LOBBY, nodes);
	else if (gKind == LINK_IR) ir_sync(gLs.host && gLs.state == LS_LOBBY, nodes);
}

// The infrared link is gone for good and lockstep won't say so: a host in
// the lobby, or a joiner that hasn't been seated.
static Boolean SEC_NET IrLost(void)
{
	if (gKind != LINK_IR || !ir_lost())
		return false;
	return gLs.host ? gLs.state == LS_LOBBY : gLs.state == LS_HELLO && !gLs.inLobby;
}

UInt8 NetPoll(void)
{
	UInt32 now;

	if (gPhase == NET_OFF || gPhase == NET_STOPPED)
		return gPhase;
	if (gInPoll)
		return gPhase;
	gInPoll = true;
	now = NetNowMs();
#ifdef LOOPBACK_TEST
	if (gKind == LINK_LOOP)
		loop_time(now);
#endif
	if (gKind != LINK_LOOP) TransportSync();
	ls_poll(&gLs, now);
	if (gKind != LINK_LOOP) TransportSync();
	// A STOP that came in says why the game ended, so it goes before a link
	// error or a join that ran out of time.
	if (gLs.state != LS_STOPPED) {
		const Char *why = LinkError();
		UInt32 wait;

		if (why[0]) {
			Why(why);
			gPhase = NET_STOPPED;
		} else if (IrLost()) {
			Why("Infrared connection lost.");
			gPhase = NET_STOPPED;
		} else if (!gLs.host && gLs.state == LS_HELLO && !gLs.inLobby &&
		           (wait = JoinWait(&why)) != 0 && now - gJoinAt >= wait) {
			Why(why);
			gPhase = NET_STOPPED;
		}
		if (gPhase == NET_STOPPED) { gInPoll = false; return gPhase; }
	}
#ifdef LOOPBACK_TEST
	if (gKind == LINK_LOOP) {
		LoopPeerPoll(now, gLs.slot);
		ls_poll(&gLs, now);
	}
#endif
	if (gLs.state == LS_STOPPED) {
		if (gPhase != NET_STOPPED)
			Stopped();
	} else if (gLs.host) {
		if (gPhase == NET_STARTING && gLs.state == LS_PLAYING)
			gPhase = NET_PLAY;
	} else if (gLs.state == LS_STARTING && gPhase != NET_PLAY) {
		JoinSetUp();
	}
	if (gPhase != NET_PLAY)
		LobbyCheck();
#ifdef DEBUG
	if (gLs.sumsCompared != gCompared) {
		gCompared = gLs.sumsCompared;
		LOG(("net: %lu checksums agree, rtt %u ms", gCompared, ls_rtt(&gLs)));
	}
	if (ls_waiting(&gLs) != gWaitLogged) {
		gWaitLogged = ls_waiting(&gLs);
		LOG(("net: waiting for %x", (UInt16)gWaitLogged));
	}
#endif
	gInPoll = false;
	return gPhase;
}

void NetService(void)
{
	if (!NetServiceActive())
		return;
	if (gKind == LINK_IR) ir_service();
	else if (gKind == LINK_BT) bt_service();
	// Polling may initialise the joiner's game. Never recurse into it or
	// inspect a game whose tick is still running.
	if (gPhase == NET_PLAY && !gInPoll && !gInTick)
		NetPoll();
}

void NetIdle(void)
{
	if (gPhase == NET_OFF || gPhase == NET_STOPPED)
		return;
	if (gPhase == NET_PLAY) {
		// NetService only runs for IR and Bluetooth; UDP needs the poll too.
		if (gKind == LINK_IR) ir_service();
		else if (gKind == LINK_BT) bt_service();
		if (!gInPoll && !gInTick)
			NetPoll();
	} else
		NetPoll();
}

Boolean NetServiceActive(void)
{
	return (gKind == LINK_IR || gKind == LINK_BT) && gPhase != NET_OFF && gPhase != NET_STOPPED;
}

UInt8 NetPhase(void)
{
	return gPhase;
}

Boolean NetIsHost(void)
{
	return gLs.host != 0;
}

UInt8 NetSlot(void)
{
	return gLs.slot;
}

const Char *NetWhy(void)
{
	return gWhy;
}

UInt16 NetLobbySeq(void)
{
	return gSeq;
}

const Char *NetMapName(void)
{
	if (gPhase == NET_OFF)
		return "";
	return gLs.host ? gMap : gLs.mapName;
}

UInt8 NetRows(void)
{
	UInt8 p;

	if (gPhase == NET_OFF || (!gLs.host && !gLs.inLobby && !gLs.gotStart))
		return 0;
	p = Players();
	return (UInt8)(p + Bots(p));
}

static UInt8 SEC_NET Pct(UInt16 have)
{
	return (UInt8)(gLs.start.mapLen ? (UInt32)have * 100 / gLs.start.mapLen : 100);
}

void NetRow(UInt8 i, Char *name, Char *note)
{
	UInt8 p = Players(), n = (UInt8)(p + Bots(p));

	note[0] = 0;
	if (i < p)
		StrCopy(name, gLs.start.names[i]);
	else
		StrCopy(name, "Bot");
	if (gLs.host && gLs.state == LS_STARTING && i > 0 && i < p && !((gLs.ready >> i) & 1)) {
		if ((gLs.fetching >> i) & 1)
			StrPrintF(note, "Map %u%%", (UInt16)Pct(gLs.mapAck[i]));
		else
			StrCopy(note, "Starting");
		return;
	}
	if (!gLs.host && i == gLs.slot && gLs.fetch == LS_MAP_GETTING) {
		StrPrintF(note, "Map %u%%", (UInt16)Pct(gLs.mapHave));
		return;
	}
	if (gLs.lobbyTeams)
		StrCopy(note, i < (n + 1) / 2 ? "Team 1" : "Team 2");
}

void NetStatus(Char *buf)
{
	buf[0] = 0;
	switch (gPhase) {
	case NET_LOBBY:
		if (gLs.host)
			StrCopy(buf, Bits(gLs.seated) < 2 ? (gKind == LINK_BT ? bt_status() : "Waiting for players") : "Ready to start");
		else if (!gLs.inLobby)
			StrCopy(buf, gKind == LINK_BT ? bt_status() : "Looking for a game");
		else
			StrPrintF(buf, "Waiting for %s to start", gLs.start.names[0]);
		break;
	case NET_STARTING:
		if (gLs.host)
			StrCopy(buf, gLs.fetching ? "Sending the map" : "Starting");
		else if (gLs.fetch == LS_MAP_GETTING)
			StrPrintF(buf, "Getting the map, %u%%", (UInt16)Pct(gLs.mapHave));
		else
			StrCopy(buf, "Starting");
		break;
	case NET_PLAY:
		StrCopy(buf, "Starting");
		break;
	case NET_STOPPED:
		StrCopy(buf, gWhy);
		break;
	}
}

void NetEnd(void)
{
	UInt32 until;

	if (gPhase == NET_OFF)
		return;
#if defined(DEBUG) && defined(IR_TEST_CLOSE_QUEUE)
	if (gKind == LINK_IR)
		ir_test_close_queue();
#endif
	// Keep offering control frames as the transport makes queue space, while
	// a peer still playing is owed one and the link can carry it.
	ls_leave(&gLs);
	until = NetNowMs() + 2000;
	do {
		if (gKind == LINK_BT) bt_service();
		ls_poll(&gLs, NetNowMs());
		if (!gLs.stopLeft || LinkDown())
			break;
		SysTaskDelay(1);
	} while ((Int32)(NetNowMs() - until) < 0);
	link_close(&gLink);
#ifdef LOOPBACK_TEST
	if (gKind == LINK_LOOP) {
		LoopPeerClose();
		// The test's copy of a map stays off the map list.
		if (gGotMap) {
			LocalID id = DmFindDatabase(0, gMap);

			if (id)
				DmDeleteDatabase(0, id);
		}
	}
#endif
	MapBytesClose(&gBytes);
	MapInDrop(&gIn);
	gPhase = NET_OFF;
	gLs.state = LS_IDLE;
}

UInt8 NetOffline(GameState *g)
{
	UInt8 result;

	gInTick = true;
	result = ls_offline(&gLs, g);
	gInTick = false;

	if (result == 2) NetFinish(g);
	else NetEnd();
	return gLs.stop == LS_STOP_DESYNC ? 0 : result;
}

// The game is over here. Polls until the others have what they need to
// reach the end too, LS_END_MS at most, then leaves.
void NetFinish(const GameState *g)
{
	UInt32 until;

	if (gPhase == NET_PLAY) {
		ls_finish(&gLs, g);
		until = NetNowMs() + LS_END_MS + 500;
		while (!ls_finished(&gLs) && NetNowMs() < until) {
			// Bluetooth only moves frames when serviced.
			if (gKind == LINK_BT) bt_service();
			if (NetPoll() != NET_PLAY)
				break;
			SysTaskDelay(1);
		}
		LOG(("net: game over at tick %lu, %s", g->tick, gLs.ended || gLs.host ? "ended" : "no word from the host"));
	}
	NetEnd();
}

// ---- in the game ----

Boolean NetReady(void)
{
	return gPhase == NET_PLAY && ls_ready(&gLs);
}

void NetTick(GameState *g, const GameInput *in)
{
	gInTick = true;
	ls_tick(&gLs, g, in);
	gInTick = false;
	NetService();
}

Boolean NetLine(const GameState *g, Char *line)
{
	UInt8 w = ls_waiting(&gLs), i, n = 0, first = 0, fresh;
	UInt32 now = NetNowMs();

	// Players' tanks newly handed to bots.
	fresh = (UInt8)(g->botMask & ~gBotsSeen & ((1 << gLs.start.numPlayers) - 1));
	if (fresh) {
		gBotsSeen = (UInt8)(gBotsSeen | fresh);
		for (i = 0; !((fresh >> i) & 1); i++)
			;
		StrPrintF(gNote, "A bot took over from %s", NetName(i));
		gNoteUntil = now + 4000;
		LOG(("net: tank %u to a bot at tick %lu", (UInt16)i, g->tick));
	}
	if (w) {
		for (i = 0; i < LS_SLOTS; i++)
			if ((w >> i) & 1) {
				if (!n)
					first = i;
				n++;
			}
		if (n == 1)
			StrPrintF(line, "Waiting for %s", NetName(first));
		else
			StrPrintF(line, "Waiting for %u players", (UInt16)n);
		return true;
	}
	if (gNoteUntil && now < gNoteUntil) {
		StrCopy(line, gNote);
		return true;
	}
	gNoteUntil = 0;
	return false;
}
