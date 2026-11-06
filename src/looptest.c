// Debug only (LOOPBACK_TEST): the second copy of the game at the other end
// of the loopback, on a map store of its own. As a joiner it lacks every
// map but Everard Island, so it gets the map from this Palm; as a host it
// starts 5 s after someone joins, on the map last picked. Its player
// drives by a simple script. LOOPBACK_DROP_AT=N makes it go silent after N
// seconds so its tank goes to a bot, and LOOPBACK_DESYNC_AT=N changes its
// state at tick N so the checksums catch it.
#include "palmbolo.h"
#include "looptest.h"
#include "netgame.h"
#include "net/lockstep.h"
#include "game/mapstore.h"

#ifdef LOOPBACK_TEST

#ifndef DEBUG
#error LOOPBACK_TEST is for debug builds
#endif

#define kLoopMinMs      20      // the loopback's delay
#define kLoopMaxMs      60
#define kHostWaitMs     5000    // as host, the time from a joiner to the start

static Lockstep gPeerLs;
static Link gPeerLink;
static GameState gPeer;
static MapBytes gPeerBytes;
static MapIn gPeerIn;
static Char gPeerMap[kMapNameLen];
static UInt16 gPeerIndex;
static UInt32 gPeerSum, gPeerLen, gSeatedAt;
static Boolean gPeerOpen, gPeerSet, gPeerGotMap, gPeerGone;
static UInt32 gRnd = 12345;
static UInt8 gHold, gHoldLeft;

static UInt32 SEC_NET Rnd(void)
{
	gRnd ^= gRnd << 13;
	gRnd ^= gRnd >> 17;
	gRnd ^= gRnd << 5;
	return gRnd;
}

static const UInt8 kCombos[8] = {
	IN_FASTER, IN_FASTER | IN_LEFT, IN_FASTER | IN_RIGHT, IN_FASTER | IN_FIRE,
	IN_SLOWER, IN_LEFT | IN_FIRE, IN_FASTER, IN_RIGHT
};

// The second copy's player: keys held for a while, now and then a shot or
// a gunsight step.
static void SEC_NET PeerKeys(GameInput *in)
{
	UInt32 r = Rnd();

	if (!gHoldLeft) {
		gHold = kCombos[r & 7];
		gHoldLeft = (UInt8)(10 + (r >> 8) % 50);
	}
	gHoldLeft--;
	MemSet(in, sizeof(*in), 0);
	in->keys = gHold;
	in->opts = OPT_AUTOSLOW;
	if ((r >> 16) % 97 == 0)
		in->keys |= (r >> 24) & 1 ? IN_SIGHT_NEAR : IN_SIGHT_FAR;
}

Boolean LoopPeerOpen(UInt8 role)
{
	const AppPrefs *p = AppPrefsGet();

	LoopPeerClose();
	loop_reset(TimGetTicks(), kLoopMinMs, kLoopMaxMs, 0, 0);
	if (link_open(&gPeerLink, LINK_LOOP, role) != 0)
		return false;
	gPeerSet = gPeerGotMap = gPeerGone = false;
	gSeatedAt = 0;
	if (role == LINK_ROLE_HOST) {
		StrCopy(gPeerMap, p->map[0] ? p->map : "Everard Island");
		gPeerIndex = StrCompare(gPeerMap, "Everard Island") == 0 ? 0 : 1;
		if (!MapFileSum(gPeerIndex, gPeerMap, &gPeerSum, &gPeerLen) ||
		    !MapBytesOpen(&gPeerBytes, gPeerIndex, gPeerMap)) {
			StrCopy(gPeerMap, "Everard Island");
			gPeerIndex = 0;
			MapFileSum(0, gPeerMap, &gPeerSum, &gPeerLen);
			MapBytesOpen(&gPeerBytes, 0, gPeerMap);
		}
		ls_host_open(&gPeerLs, &gPeerLink, "Loopback");
		ls_host_lobby(&gPeerLs, gPeerMap, p->numBots, p->teams, (LsMapGet)MapBytesGet, &gPeerBytes);
	} else {
		ls_join_open(&gPeerLs, &gPeerLink, "Loopback");
	}
	gPeerOpen = true;
	LOG(("loopback: second copy opened as %s", role == LINK_ROLE_HOST ? "host" : "joiner"));
	return true;
}

// Sets up the second copy's game on its own map store.
static Boolean SEC_NET PeerGame(UInt16 index, const Char *name)
{
	GameSetup ps;
	Int16 err;

	mapstore_peer_swap();
	err = MapLoad(index, name, &gPeer);
	if (err == MAP_OK) {
		ls_setup(&gPeerLs.start, &ps);
		game_init(&gPeer, gPeerLs.start.seed, &ps);
#if defined(DEBUG) && defined(TEST_TIME_LIMIT)
		gPeer.timeLimit = TEST_TIME_LIMIT;   // ticks, the same on every Palm
#endif
	}
	mapstore_peer_swap();
	LOG(("loopback: second copy's game on %s: %d", name, err));
	gPeerSet = err == MAP_OK;
	return gPeerSet;
}

static void SEC_NET PeerHostStart(void)
{
	const AppPrefs *p = AppPrefsGet();
	LsStart st;
	UInt8 players = 2, bots = p->numBots > 6 ? 6 : p->numBots, n, half, i, j;

	MemSet(&st, sizeof(st), 0);
	st.seed = TimGetSeconds() ^ 0x5A5A;
	st.mapSum = gPeerSum;
	st.mapLen = (UInt16)gPeerLen;
	st.gameType = p->gameType;
	st.hiddenMines = p->hiddenMines;
	st.timeLimit = (UInt8)AppTimeMinutes(p->timeLimit);
	st.numBots = bots;
	st.botLevel = (UInt8)(p->botLevel + BOT_EASY);
	st.delay = 3;
	n = (UInt8)(players + bots);
	half = (UInt8)((n + 1) / 2);
	if (p->teams)
		for (i = 0; i < n; i++)
			for (j = 0; j < n; j++)
				if (i != j && (i < half) == (j < half))
					st.allies[i] = (UInt8)(st.allies[i] | (1 << j));
	gPeerLs.lobbyBots = bots;
	ls_host_start(&gPeerLs, &st);
	PeerGame(gPeerIndex, gPeerMap);
}

// Joiner: finds the map (only Everard Island counts) or gets it.
static void SEC_NET PeerJoinSetUp(void)
{
	Int16 index = 0;

	if (gPeerLs.fetch == LS_MAP_GETTING)
		return;
	if (gPeerLs.fetch == LS_MAP_NONE) {
		index = MapFind(gPeerLs.start.mapSum, gPeerLs.start.mapLen, gPeerMap, false);
		if (index < 0) {
			LOG(("loopback: second copy gets map %s", gPeerLs.mapName));
			if (!MapInOpen(&gPeerIn, gPeerLs.start.mapLen)) {
				ls_leave(&gPeerLs);
				return;
			}
			ls_join_fetch(&gPeerLs, (LsMapPut)MapInPut, &gPeerIn);
			return;
		}
	}
	if (gPeerLs.fetch == LS_MAP_GOT) {
		if (!MapInKeep(&gPeerIn, gPeerLs.mapName[0] ? gPeerLs.mapName : "Network map", gPeerMap)) {
			ls_leave(&gPeerLs);
			return;
		}
		gPeerGotMap = true;
		index = 1;
		LOG(("loopback: second copy kept the map as %s", gPeerMap));
	}
	if (PeerGame((UInt16)index, gPeerMap))
		ls_join_ready(&gPeerLs);
	else
		ls_leave(&gPeerLs);
}

void LoopPeerPoll(UInt32 now, UInt8 slot)
{
	Boolean swapped = false;

	if (!gPeerOpen || gPeerGone)
		return;
	ls_poll(&gPeerLs, now);
	if (gPeerLs.host && gPeerLs.state == LS_LOBBY) {
		if (gPeerLs.seated == 3 && !gSeatedAt)
			gSeatedAt = now;
		else if (gPeerLs.seated != 3)
			gSeatedAt = 0;
		if (gSeatedAt && now - gSeatedAt >= kHostWaitMs)
			PeerHostStart();
	}
	if (!gPeerLs.host && gPeerLs.state == LS_STARTING && !gPeerSet)
		PeerJoinSetUp();
	// The second copy runs every tick it has the turn for.
	while (gPeerSet && gPeerLs.state == LS_PLAYING && !gPeer.over && ls_ready(&gPeerLs)) {
		GameInput in;

#ifdef LOOPBACK_DROP_AT
		if (gPeer.tick >= (UInt32)LOOPBACK_DROP_AT * GAME_TICKS_PER_SECOND) {
			gPeerGone = true;
			LOG(("loopback: second copy silent from tick %lu", gPeer.tick));
			break;
		}
#endif
		if (!swapped) {
			mapstore_peer_swap();
			game_set_viewer(gPeerLs.slot);
			swapped = true;
		}
#ifdef LOOPBACK_DESYNC_AT
		if (gPeer.tick == LOOPBACK_DESYNC_AT)
			gPeer.rng ^= 0x00A50000UL;
#endif
		PeerKeys(&in);
		ls_tick(&gPeerLs, &gPeer, &in);
	}
	if (swapped) {
		mapstore_peer_swap();
		game_set_viewer(slot);
	}
	// At the end it waits for the other side, then leaves on its own.
	if (gPeerSet && gPeerLs.state == LS_PLAYING && gPeer.over) {
		ls_finish(&gPeerLs, &gPeer);
		if (ls_finished(&gPeerLs))
			ls_leave(&gPeerLs);
	}
	if (!gPeerGone)
		ls_poll(&gPeerLs, now);
}

void LoopPeerClose(void)
{
	UInt8 i;

	if (!gPeerOpen)
		return;
	ls_leave(&gPeerLs);
	for (i = 0; i < 4 && gPeerLs.stopLeft && !gPeerGone; i++)
		ls_poll(&gPeerLs, NetNowMs());
	link_close(&gPeerLink);
	mapstore_peer_close();
	MapBytesClose(&gPeerBytes);
	MapInDrop(&gPeerIn);
	// The second copy's map stays off the map list.
	if (gPeerGotMap) {
		LocalID id = DmFindDatabase(0, gPeerMap);

		if (id)
			DmDeleteDatabase(0, id);
	}
	gPeerLs.state = LS_IDLE;
	gPeerOpen = false;
}

// Both states to a database, with the tick and checksums first.
void LoopSaveDesync(const GameState *g, const Lockstep *ls)
{
	DmOpenRef db;
	LocalID id = DmFindDatabase(0, "PalmBoloDesync");
	UInt32 head[4];
	UInt16 i, index;

	if (id)
		DmDeleteDatabase(0, id);
	if (DmCreateDatabase(0, "PalmBoloDesync", 'Rmrf', 'Dsyn', false) != errNone)
		return;
	db = DmOpenDatabaseByTypeCreator('Dsyn', 'Rmrf', dmModeReadWrite);
	if (!db)
		return;
	head[0] = ls->badTick;
	head[1] = ls->badLocal;
	head[2] = ls->badRemote;
	head[3] = ls->badSlot;
	for (i = 0; i < 3; i++) {
		const void *src = i == 0 ? (const void *)head : i == 1 ? (const void *)g : (const void *)&gPeer;
		UInt32 size = i == 0 ? sizeof(head) : sizeof(GameState);
		MemHandle h;

		index = dmMaxRecordIndex;
		h = DmNewRecord(db, &index, size);
		if (!h)
			break;
		DmWrite(MemHandleLock(h), 0, src, size);
		MemHandleUnlock(h);
		DmReleaseRecord(db, index, true);
	}
	DmCloseDatabase(db);
}

#endif
