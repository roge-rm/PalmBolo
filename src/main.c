#include "palmbolo.h"
#include "resource.h"
#include "game/game.h"
#include "game/script.h"
#include "game/util.h"
#include "netgame.h"

static DeviceInfo gDev;
static GameState gGame;
static Boolean gNet;        // the game is a network game (netgame.h)
static Boolean gQuit;
static Boolean gFormOpen;
static Boolean gDirty;
static Boolean gMenuOpen;   // no drawing while a menu is showing
static Boolean gFull;       // next draw repaints everything
static UInt16 gForm;        // the form on screen, 0 before the first
static Boolean gInGame;     // a game is being played: the state is live
static UInt8 gFire;         // ticks of fire left from a 5-way centre press
static Int8 gSight;         // gunsight steps asked for by taps, sent next tick
static UInt8 gAction;       // builder action chosen, LGM_TREE to LGM_MINE
static UInt8 gBuild, gBuildX, gBuildY;  // builder order to send next tick
#ifdef DEBUG
static Boolean gTimedFull;
static UInt32 gStartTicks;  // when Start was tapped
static UInt16 gDropped;     // passes that fell over a frame behind and dropped time
#endif

// FIXED_SEED=N starts every new game from seed N, for measured runs that
// have to start the same game. Debug builds only.
#if defined(FIXED_SEED) && !defined(DEBUG)
#error FIXED_SEED is for debug builds
#endif

// The app's own settings, kept in its preferences.
#define kAppCreator     'Rmrf'
#define kPrefsId        0
#define kPrefsVersion   2

// Bots in a new game and their level; EXTRA can set either.
#ifdef BOT_COUNT
#define kDefBots        BOT_COUNT
#else
#define kDefBots        3
#endif
#ifdef BOT_LEVEL
#define kDefLevel       (BOT_LEVEL - 1)
#else
#define kDefLevel       1
#endif
static AppPrefs gPrefs;

AppPrefs *AppPrefsGet(void)
{
	return &gPrefs;
}

static void SEC_COLD Defaults(void)
{
	Char user[dlkUserNameBufSize];

	MemSet(&gPrefs, sizeof(gPrefs), 0);
	gPrefs.sound = 1;
	gPrefs.autoSlow = 1;
	gPrefs.numBots = kDefBots;
	gPrefs.botLevel = kDefLevel;
	gPrefs.keys[0] = KEY_LEFT;
	gPrefs.keys[1] = KEY_FIRE;
	gPrefs.keys[2] = KEY_MINE;
	gPrefs.keys[3] = KEY_RIGHT;
	StrCopy(gPrefs.name, "Player");
	// The HotSync user name, if there is one.
	user[0] = 0;
	if (DlkGetSyncInfo(NULL, NULL, NULL, user, NULL, NULL) == errNone && user[0]) {
		StrNCopy(gPrefs.name, user, kNameLen - 1);
		gPrefs.name[kNameLen - 1] = 0;
	}
}

static void SEC_COLD LoadPrefs(void)
{
	UInt16 size = sizeof(gPrefs);
	Int16 v;

	v = PrefGetAppPreferences(kAppCreator, kPrefsId, &gPrefs, &size, true);
	if (v != kPrefsVersion || size != sizeof(gPrefs))
		Defaults();
	SoundSetOn(gPrefs.sound != 0);
}

void SEC_COLD AppPrefsSave(void)
{
	PrefSetAppPreferences(kAppCreator, kPrefsId, kPrefsVersion, &gPrefs, sizeof(gPrefs), true);
}

void AppSetSound(Boolean on)
{
	gPrefs.sound = on ? 1 : 0;
	SoundSetOn(on);
	LOG(("sound option %s", on ? "on" : "off"));
}

UInt16 AppTimeMinutes(UInt8 choice)
{
	switch (choice) {
	case 1: return 5;
	case 2: return 10;
	case 3: return 15;
	case 4: return 30;
	}
	return 0;
}

// Hardware buttons the game uses. They are consumed so they don't launch
// other applications.
#define vchrNavChange   0x0503   // 5-way on palmOne devices
#define navBitSelect    0x0010
#define vchrTxCenter    0x013D   // the TX sends this with keyCode vchrRockerCenter

static Boolean IsGameKey(WChar chr)
{
	switch (chr) {
	case vchrHard1: case vchrHard2: case vchrHard3: case vchrHard4:
	case vchrPageUp: case vchrPageDown:
	case vchrRockerUp: case vchrRockerDown: case vchrRockerLeft:
	case vchrRockerRight: case vchrRockerCenter:
	case vchrNavChange:
		return true;
	}
	return false;
}

static void SEC_COLD ShowAbout(void)
{
	FrmCustomAlert(AboutAlert, kAppVersion, "", "");
	gDirty = gFull = true;
}

#ifdef DEBUG
static void SEC_COLD SelfTest(void);
#endif

GameState *AppGame(void)
{
	return &gGame;
}

// Leaves a game in progress for the title, saving it unless it is over.
// A network game isn't saved.
static void SEC_COLD LeaveGame(void)
{
	if (gInGame && gForm == GameForm) {
		if (gNet) {
			NetEnd();
			gNet = false;
			ViewMessage(NULL);
		} else if (!gGame.over && !SaveWrite(&gGame)) {
			// Stay in the game, it is untouched.
			FrmAlert(SaveFailAlert);
			return;
		}
		gFormOpen = false;
		gInGame = false;
		FrmGotoForm(TitleForm);
	}
}

static Boolean SEC_COLD HandleMenu(UInt16 id)
{
	switch (id) {
	case MainMenuAbout:
		// An alert would hold up everyone in a network game.
		if (!(gNet && gInGame))
			ShowAbout();
		return true;
	case MainMenuTitle:
		LeaveGame();
		return true;
#ifdef DEBUG
	case MainMenuSelfTest:
		SelfTest();
		FrmGotoForm(TitleForm);
		return true;
#endif
	case MainMenuQuit: {
		EventType e;

		MemSet(&e, sizeof(e), 0);
		e.eType = appStopEvent;
		EvtAddEventToQueue(&e);
		return true;
	}
	}
	return false;
}

void SEC_COLD AppOpenMap(UInt16 index, const Char *name)
{
	Int16 err;

#ifdef DEBUG
	gStartTicks = TimGetTicks();
	gTimedFull = false;
	TimingHeap("new game");
#endif
	err = MapLoad(index, name, &gGame);

	if (err != MAP_OK) {
		LOG(("map %s: error %d", name, err));
		FrmCustomAlert(MapErrorAlert, name,
			err < 0 ? "Can't open it." : map_error_text(err), "");
		return;
	}
	LOG(("map %s: pills %d bases %d starts %d", name, (Int16)gGame.map.numPills,
		(Int16)gGame.map.numBases, (Int16)gGame.map.numStarts));
	{
		GameSetup setup;
		UInt8 i, j, n = (UInt8)(1 + gPrefs.numBots), half = (UInt8)((n + 1) / 2);

		MemSet(&setup, sizeof(setup), 0);
		setup.gameType = gPrefs.gameType;
		setup.hiddenMines = gPrefs.hiddenMines;
		setup.timeLimit = (UInt8)AppTimeMinutes(gPrefs.timeLimit);
		setup.numPlayers = 1;
		setup.numBots = gPrefs.numBots;
		setup.botLevel = (UInt8)(gPrefs.botLevel + BOT_EASY);
		// Teams: the player and the first bots against the rest.
		if (gPrefs.teams)
			for (i = 0; i < n; i++)
				for (j = 0; j < n; j++)
					if (i != j && (i < half) == (j < half))
						setup.allies[i] = (UInt16)(setup.allies[i] | (1 << j));
		LOG(("start: map loaded %lu sys ticks after Start", TimGetTicks() - gStartTicks));
#if defined(DRAW_BENCH)
		game_init(&gGame, 4321, &setup);
#elif defined(FIXED_SEED)
		game_init(&gGame, FIXED_SEED, &setup);
#else
		game_init(&gGame, TimGetSeconds(), &setup);
#endif
		LOG(("start: game set up %lu sys ticks after Start", TimGetTicks() - gStartTicks));
	}
#if defined(DEBUG) && defined(TEST_TIME_LIMIT)
	gGame.timeLimit = TEST_TIME_LIMIT;
#endif
	SaveDrop();
	gInGame = true;
	// The player on this Palm drives tank 0, the bots the tanks after it.
	game_set_viewer(0);
	gBuild = BUILD_NONE;
#if defined(DEBUG) && defined(DEMO_START)
	// For replays: the tank on land six squares west of the first base,
	// facing it.
	if (gGame.map.numBases > 0) {
		Tank *k = &gGame.tanks[game_viewer_tank(&gGame)];

		k->x = (UInt16)(((UInt16)gGame.map.bases[0].x - 6) * 256 + 128);
		k->y = (UInt16)((UInt16)gGame.map.bases[0].y * 256 + 128);
		k->angle = 64 * ANGLE_ONE;
		k->boat = BOAT_OFF;
	}
#endif
#if defined(DEBUG) && defined(DEMO_SHOWCASE)
	DemoShowcase(&gGame);
#endif
#if defined(DEBUG) && defined(DEMO_BUILDER)
	// For the builder replay on Everard Island: the tank on the road at
	// 160,116 facing east with no trees, forest two squares south east.
	{
		Tank *k = &gGame.tanks[game_viewer_tank(&gGame)];

		k->x = 160U * 256U + 128U;
		k->y = 116U * 256U + 128U;
		k->angle = 64 * ANGLE_ONE;
		k->boat = BOAT_OFF;
		k->trees = 0;
	}
#endif
	FrmGotoForm(GameForm);
}

// The lobby set up a network game in gGame: play it.
void SEC_COLD AppNetGame(void)
{
#ifdef DEBUG
	gStartTicks = TimGetTicks();
	gTimedFull = false;
#endif
	gNet = true;
	gInGame = true;
	ViewMessage(NULL);
	game_set_viewer(NetSlot());
	gBuild = BUILD_NONE;
	FrmGotoForm(GameForm);
}

void SEC_COLD AppResume(void)
{
#ifdef DEBUG
	gStartTicks = TimGetTicks();
	gTimedFull = false;
#endif
	if (!SaveRead(&gGame)) {
		SaveDrop();
		FrmAlert(ResumeAlert);
		FrmGotoForm(TitleForm);
		return;
	}
	LOG(("resumed at tick %lu", gGame.tick));
	game_set_viewer(0);
	gBuild = BUILD_NONE;
	gInGame = true;
	FrmGotoForm(GameForm);
}

static Boolean GameHandler(EventType *e)
{
	switch (e->eType) {
	case frmOpenEvent:
		FrmDrawForm(FrmGetActiveForm());
		LOG(("start: form drawn %lu sys ticks after Start", TimGetTicks() - gStartTicks));
		if (gNet)
			NetPoll();
		if (!ViewOpen(&gDev, &gGame)) {
			if (gNet) {
				NetEnd();
				gNet = false;
			}
			gInGame = false;
			FrmGotoForm(TitleForm);
			return true;
		}
		if (gNet)
			NetPoll();
		LOG(("start: view open %lu sys ticks after Start", TimGetTicks() - gStartTicks));
		TimingHeap("view open");
		SoundOpen(gDev.ticksPerSecond);
		KeysStart(gDev.ticksPerSecond);
		gFormOpen = true;
		gDirty = gFull = true;
		return true;
	case frmCloseEvent:
		gFormOpen = false;
		SoundStop();
		KeysStop();
		ViewClose();
		return false;
	default:
		break;
	}
	return false;
}

// A one-off keyboard action (keys.c). Builder orders go to the square
// under the gunsight.
static void KeyAction(UInt8 a)
{
	if (a >= KA_TREE && a <= KA_MINE) {
		gAction = (UInt8)(a - KA_TREE);
		ViewSetAction(gAction);
		KeysTarget(&gGame, &gBuildX, &gBuildY);
		gBuild = (UInt8)(gAction + 1);
	} else if (a == KA_NEAR || a == KA_FAR) {
		gSight = (Int8)(gSight + (a == KA_NEAR ? -1 : 1));
	} else if (a == KA_OVERVIEW) {
		if (PanelToggle())
			gDirty = gFull = true;
	} else if (a == KA_MENU) {
		EvtEnqueueKey(vchrMenu, 0, commandKeyMask);
	}
}

// Returns true when the event was dealt with.
static Boolean HandleEvent(EventType *e)
{
	UInt16 err;
	FormType *frm;

	if (e->eType == keyDownEvent) {
		WChar c = e->data.keyDown.chr;

		if (!(e->data.keyDown.modifiers & autoRepeatKeyMask))
			LOG(("key chr %x code %x mods %x", (UInt16)c, e->data.keyDown.keyCode,
				e->data.keyDown.modifiers));
		// The 5-way centre fires. It only comes as key events, so each press
		// or repeat fires for a few ticks.
		if (gForm == GameForm && (c == vchrRockerCenter || c == vchrTxCenter ||
		    e->data.keyDown.keyCode == vchrRockerCenter ||
		    (c == vchrNavChange && (e->data.keyDown.keyCode & navBitSelect))))
			gFire = 8;
	}
	// Keyboards and the 5-way's left and right in a game (keys.c);
	// keyUpEvent is 0x4000.
	if (gForm == GameForm && gFormOpen && !gMenuOpen &&
	    (e->eType == keyDownEvent || e->eType == 0x4000)) {
		UInt8 a = KeysEvent(e);

		if (a & ~KA_EAT)
			KeyAction((UInt8)(a & ~KA_EAT));
		if (a & KA_EAT)
			return true;
	}
	if (e->eType == keyDownEvent && (IsGameKey(e->data.keyDown.chr) ||
	    e->data.keyDown.chr == vchrTxCenter))
		return true;
	// A tap on the map sends the builder there with the chosen action. The
	// button row along the bottom chooses the action or moves the gunsight.
	if (gForm == GameForm && !gMenuOpen && e->eType == penDownEvent &&
	    e->screenY >= 0) {
		UInt8 mx, my;
		Int16 b = ViewTap(e->screenX, e->screenY, &mx, &my);

		if (b < 0) {
			gBuild = (UInt8)(gAction + 1);
			gBuildX = mx;
			gBuildY = my;
			LOG(("build %u at %u,%u", (UInt16)gBuild, (UInt16)mx, (UInt16)my));
		} else if (b == kTapPanel) {
			if (PanelToggle())
				gDirty = gFull = true;
		} else if (b <= LGM_MINE) {
			gAction = (UInt8)b;
			ViewSetAction(gAction);
		} else {
			gSight = b == LGM_MINE + 1 ? -1 : 1;
		}
		return true;
	}
	// A tap or a chosen item closes the menu, and the screen is repainted.
	if (gMenuOpen && (e->eType == penDownEvent || e->eType == menuEvent)) {
		gMenuOpen = false;
		gDirty = gFull = true;
	}
	MARK(LP_HANDLE);
	if (SysHandleEvent(e)) {
		MARK(LP_SYS);
		return true;
	}
	MARK(LP_SYS);
	if (MenuHandleEvent(0, e, &err))
		return true;

	switch (e->eType) {
	case appStopEvent:
		gQuit = true;
		return true;
	case frmLoadEvent:
		gForm = e->data.frmLoad.formID;
		frm = FrmInitForm(gForm);
		FrmSetActiveForm(frm);
		FrmSetEventHandler(frm, gForm == GameForm ? GameHandler : FrontHandler(gForm));
		return true;
	case winDisplayChangedEvent:
		if (gForm == GameForm) {
			ViewResized();
			gDirty = gFull = true;
		}
		return false;
	case menuOpenEvent:
		gMenuOpen = true;
		KeysClear();
		break;
	case menuEvent:
		return HandleMenu(e->data.menu.itemID);
	case winEnterEvent:
	case winExitEvent:
		gDirty = gFull = true;
		KeysClear();
		break;
	default:
		break;
	}
	return FrmDispatchEvent(e);
}

// Held hardware buttons as game inputs. The four application buttons do
// what the settings say (turn, fire and lay a mine by default), the 5-way
// turns and fires, up and down speed up and slow down. Taps give the
// gunsight steps and builder orders.
#define kKeyNavLeft     0x01000000UL
#define kKeyNavRight    0x02000000UL
#define kKeyNavSelect   0x04000000UL

static UInt8 KeyMask(UInt8 code)
{
	switch (code) {
	case KEY_LEFT: return IN_LEFT;
	case KEY_RIGHT: return IN_RIGHT;
	case KEY_FIRE: return IN_FIRE;
	case KEY_MINE: return IN_MINE;
	case KEY_FASTER: return IN_FASTER;
	case KEY_SLOWER: return IN_SLOWER;
	}
	return 0;
}

static GameInput ReadInputs(void)
{
	UInt32 k = KeyCurrentState();
	GameInput in;

	in.keys = 0;
	in.opts = (UInt8)((gPrefs.autoSlow ? OPT_AUTOSLOW : 0) | (gPrefs.autoSight ? OPT_AUTOSIGHT : 0));
	if (k & keyBitHard1)
		in.keys |= KeyMask(gPrefs.keys[0]);
	if (k & keyBitHard2)
		in.keys |= KeyMask(gPrefs.keys[1]);
	if (k & keyBitHard3)
		in.keys |= KeyMask(gPrefs.keys[2]);
	if (k & keyBitHard4)
		in.keys |= KeyMask(gPrefs.keys[3]);
	if (k & kKeyNavLeft)
		in.keys |= IN_LEFT;
	if (k & kKeyNavRight)
		in.keys |= IN_RIGHT;
	if (k & keyBitPageUp)
		in.keys |= IN_FASTER;
	if (k & keyBitPageDown)
		in.keys |= IN_SLOWER;
	in.keys |= KeysHeld();
	if ((k & kKeyNavSelect) || gFire) {
		in.keys |= IN_FIRE;
		if (gFire)
			gFire--;
	}
	if (gSight > 0)
		in.keys |= IN_SIGHT_FAR;
	else if (gSight < 0)
		in.keys |= IN_SIGHT_NEAR;
	// Each tick takes one step; more taps wait for the next.
	gSight = (Int8)(gSight > 0 ? gSight - 1 : gSight < 0 ? gSight + 1 : 0);
	in.build = gBuild;
	in.buildX = gBuildX;
	in.buildY = gBuildY;
	gBuild = BUILD_NONE;
	return in;
}

#ifdef DEBUG
// Times the bots apart from the rules over BENCH_TICKS ticks of the game
// the script left. The sys ticks seen to pass while the bots make their
// keys add up to their share, roughly, though each call is much shorter
// than a sys tick. Closer: the same ticks again with the keys the bots
// made, the bots' time being the difference, the least of three. Then the worst tick of the
// first BENCH_WORST: each tick's bots run BENCH_REPS times on copies of
// the state, less the copying. Lives in cold to keep code 1 small.
#define BENCH_TICKS 1000
#define BENCH_WORST 300
#define BENCH_REPS 50

// Runs ticks with the bots, or with the keys in rec if play. Returns sys
// ticks taken, the bots' share seen in *bots.
static UInt32 SEC_COLD BenchRun(GameState *c, GameInput *rec, UInt8 play, UInt32 *bots)
{
	GameInput none[GAME_MAX_TANKS], keys[GAME_MAX_TANKS];
	UInt8 mask = c->botMask, nt = c->numTanks;
	UInt32 t0 = TimGetTicks(), a;
	UInt16 i;

	MemSet(none, sizeof(none), 0);
	*bots = 0;
	for (i = 0; i < BENCH_TICKS; i++) {
		if (play) {
			MemMove(keys, &rec[i * nt], nt * sizeof(GameInput));
		} else {
			a = TimGetTicks();
			game_bot_inputs(c, none, keys);
			*bots += TimGetTicks() - a;
			MemMove(&rec[i * nt], keys, nt * sizeof(GameInput));
		}
		c->botMask = 0;
		game_tick(c, keys);
		c->botMask = mask;
	}
	return TimGetTicks() - t0;
}

static void SEC_COLD BenchBots(void)
{
	GameState *c = (GameState *)MemPtrNew(sizeof(GameState));
	GameState *w = (GameState *)MemPtrNew(sizeof(GameState));
	GameInput *rec = (GameInput *)MemPtrNew(BENCH_TICKS * gGame.numTanks * sizeof(GameInput));
	GameInput none[GAME_MAX_TANKS], keys[GAME_MAX_TANKS];
	UInt32 a, d, bots, seen = 0, copy, worst = 0, sum = 0;
	UInt16 i, r, n = 0, at = 0;
#ifdef BENCH_RULES
	UInt32 rw = 0, rsum = 0;
	UInt16 rat = 0;
#endif
	UInt8 mask = gGame.botMask;

	if (!c || !w || !rec) {
		LOG(("bench: no memory"));
		goto done;
	}
	MemSet(none, sizeof(none), 0);
	for (i = 0; i < GAME_MAX_TANKS; i++)
		n += (mask >> i) & 1;
	// Each way three times, the least of each, as the emulator's timing
	// varies a little.
	a = d = 0xFFFFFFFFUL;
	for (r = 0; r < 3; r++) {
		UInt32 t;

		MemMove(c, &gGame, sizeof(GameState));
		t = BenchRun(c, rec, 0, &bots);
		if (t < a) {
			a = t;
			seen = bots;
		}
		MemMove(c, &gGame, sizeof(GameState));
		t = BenchRun(c, rec, 1, &bots);
		if (t < d)
			d = t;
	}
	bots = a > d ? a - d : 0;
	LOG(("bench rules alone x%d: %lu sys ticks (%lu.%02lu ms a tick)", BENCH_TICKS, d,
		d * 10 / BENCH_TICKS, d * 1000 / BENCH_TICKS % 100));
	LOG(("bench %d tanks %d bots x%d: %lu sys ticks, bots seen %lu, bots %lu (%lu.%02lu ms a bot a tick)",
		(Int16)c->numTanks, (Int16)n, BENCH_TICKS, a, seen, bots,
		bots * 10 / n / BENCH_TICKS, bots * 1000 / n / BENCH_TICKS % 100));
#ifdef BENCH_QUICK
	goto done;
#endif
	// The worst tick, in hundredths of a sys tick for all the bots.
	MemMove(c, &gGame, sizeof(GameState));
	a = TimGetTicks();
	for (r = 0; r < BENCH_REPS * 20; r++)
		MemMove(w, c, sizeof(GameState));
	copy = (TimGetTicks() - a) * 100 / 20;
	for (i = 0; i < BENCH_WORST; i++) {
		a = TimGetTicks();
		for (r = 0; r < BENCH_REPS; r++) {
			MemMove(w, c, sizeof(GameState));
			game_bot_inputs(w, none, keys);
		}
		d = (TimGetTicks() - a) * 100;
		d = d > copy ? (d - copy) / BENCH_REPS : 0;
		sum += d;
		if (d > worst) {
			worst = d;
			at = i;
		}
		game_bot_inputs(c, none, keys);
#ifdef BENCH_RULES
		// The rules alone on the same tick, with the keys the bots made.
		a = TimGetTicks();
		for (r = 0; r < BENCH_REPS; r++) {
			MemMove(w, c, sizeof(GameState));
			w->botMask = 0;
			game_tick(w, keys);
		}
		d = (TimGetTicks() - a) * 100;
		d = d > copy ? (d - copy) / BENCH_REPS : 0;
		rsum += d;
		if (d > rw) {
			rw = d;
			rat = i;
		}
#endif
		c->botMask = 0;
		game_tick(c, keys);
		c->botMask = mask;
	}
	// sum is in hundredths of a sys tick of 10 ms over BENCH_WORST ticks.
	LOG(("bench worst x%d reps %d: sum %lu (%lu us a bot a tick), worst %lu at %d (%lu us all bots), copy %lu",
		BENCH_WORST, BENCH_REPS, sum, sum * 100 / BENCH_WORST / n, worst, at, worst * 100, copy));
#ifdef BENCH_RULES
	LOG(("bench rules x%d reps %d: %lu us a tick average, worst %lu us at %d",
		BENCH_WORST, BENCH_REPS, rsum * 100 / BENCH_WORST, rw * 100, rat));
#endif
	TimingHeap("bench");
done:
	if (rec)
		MemPtrFree(rec);
	if (w)
		MemPtrFree(w);
	if (c)
		MemPtrFree(c);
}

// Checks the const tables read back right from both code segments, then
// runs the scripted games on Everard Island and logs their checksums for
// comparing with the host test.
static void SEC_COLD SelfTest(void)
{
	Char hex[9], name[] = "Everard Island";
	UInt32 v, t0, t1;
	UInt8 n;

	v = util_table_check();
	HexStr(v, hex);
	LOG(("const table code 1 %s %s", hex, v == UTIL_TABLE_CHECK ? "ok" : "WRONG"));
	v = DetectTableCheck();
	HexStr(v, hex);
	LOG(("const table detect %s %s", hex, v == DETECT_TABLE_CHECK ? "ok" : "WRONG"));
#ifndef SELFTEST_FIRST
#define SELFTEST_FIRST 0
#endif
#ifndef SELFTEST_LAST
#define SELFTEST_LAST (SCRIPT_COUNT - 1)
#endif
	for (n = SELFTEST_FIRST; n <= SELFTEST_LAST; n++) {
		if (MapLoad(0, name, &gGame) != MAP_OK) {
			LOG(("script %d: map load failed", (Int16)n));
			return;
		}
		t0 = TimGetTicks();
		v = script_run(&gGame, n, NULL, NULL);
		t1 = TimGetTicks();
		HexStr(v, hex);
		LOG(("script %d %s checksum %s ticks %lu in %lu sys ticks", (Int16)n,
			script_name(n), hex, gGame.tick, t1 - t0));
		if (n == 33 || n == 34)
			BenchBots();
	}
	FrontSaveTest(&gGame);
}
#endif

#ifdef DRAW_BENCH
static void SEC_COLD DrawBench(void);
#endif
#ifdef TICK_PARTS
// Debug: time spent in each part of game_tick, part n ending at
// tick_part(n); parts 10 and up split the tanks. A mark reads only the
// DragonBall's timer 1 counter, which counts up to the compare value once
// a system tick (OS 3 and 4), so a part over a system tick reads short;
// the whole tick is also timed in system ticks to show that.
#define kTpParts 20
#define kTpCounter ((volatile UInt16 *)0xFFFFF608UL)
#define kTpCompare ((volatile UInt16 *)0xFFFFF604UL)
static UInt32 gTickPartC[kTpParts], gTickPartN, gTickPartAll, gTickPartT;
static UInt16 gTickPartLC, gTickPartDiv;    // div: timer counts a system tick, 0 if none

static void SEC_COLD TickPartInit(void)
{
	UInt32 rom = 0;

	FtrGet(sysFtrCreator, sysFtrNumROMVersion, &rom);
	if (rom < 0x05000000UL && *kTpCompare > 100)
		gTickPartDiv = *kTpCompare;
}

void tick_part(uint8_t n)
{
	UInt16 c = gTickPartDiv ? *kTpCounter : 0;
	Int16 d;

	if (n) {
		d = (Int16)(c - gTickPartLC);
		if (d < 0)
			d = (Int16)(d + gTickPartDiv);
		gTickPartC[n] += (UInt16)d;
		if (n == 8) {
			gTickPartAll += TimGetTicks() - gTickPartT;
			gTickPartN++;
		}
	} else {
		gTickPartT = TimGetTicks();
		c = gTickPartDiv ? *kTpCounter : 0;
	}
	gTickPartLC = c;
}

// Part n in microseconds a tick.
static UInt16 SEC_COLD TickPartUs(UInt8 n)
{
	UInt32 div = gTickPartDiv, c = gTickPartC[n];

	if (!gTickPartN || !div)
		return 0;
	c /= gTickPartN;
	return (UInt16)(c * 10000UL / div);
}

static void SEC_COLD TickPartsLog(void)
{
	UInt8 i;

	LOG(("tick parts us a tick over %lu: all %lu occ %u bots %u tanks %u men %u pills %u bases %u shells etc %u trees %u",
		gTickPartN, gTickPartN ? gTickPartAll * 10000UL / gTickPartN : 0, TickPartUs(1), TickPartUs(2),
		(UInt16)(TickPartUs(3) + TickPartUs(10) + TickPartUs(11) + TickPartUs(12) + TickPartUs(13) +
		TickPartUs(14) + TickPartUs(15) + TickPartUs(16) + TickPartUs(17) + TickPartUs(18)),
		TickPartUs(4), TickPartUs(5), TickPartUs(6), TickPartUs(7), TickPartUs(8)));
	LOG(("tank parts us a tick: pre %u push %u speed %u tnudge %u move %u nudge %u pick %u after %u near %u rest %u",
		TickPartUs(10), TickPartUs(11), TickPartUs(12), TickPartUs(13), TickPartUs(14),
		TickPartUs(15), TickPartUs(16), TickPartUs(17), TickPartUs(18), TickPartUs(3)));
	for (i = 0; i < kTpParts; i++)
		gTickPartC[i] = 0;
	gTickPartN = gTickPartAll = 0;
}
#endif

// Frame pacing. Game ticks run on a 50 a second clock and a frame is drawn
// after every gStride half ticks of them: an odd stride takes turns at one
// tick more and one less, so frames come at a near even rhythm. A device
// that falls behind keeps at most two frames' or 10 ticks' time owed and
// drops the rest: the game runs slower, every tick still runs in order.
// Every kPaceTicks ticks the stride is set to the fewest that keep up,
// from the busy time (all but the waits for ticks to come due) split into
// drawing and the rest. Full repaints and debug logs are left out.
#define kPaceTicks  64
// At most 4 ticks a frame: when the ticks alone nearly fill the time, more
// costs a lot of smoothness for little speed.
#define kMaxStride  8                   // half ticks
static UInt8 gStride = 4;
static UInt8 gStrideOdd;                // the half tick carried to the next frame
static Boolean gPaceNew = true;         // the next ticks start a period
static UInt32 gPaceStart;               // sys ticks at the period's first ticks
static UInt32 gPaceIdle;                // half sys ticks waiting
static UInt32 gPaceDraw, gPaceSkip;     // sys ticks drawing, and left out
static UInt16 gPaceTicks, gPaceFrames;  // ticks and frames in the period
static UInt8 gPaceSlow;                 // ticks left to catch up a full repaint or a log
static Boolean gPaceBehind;             // 5 ticks or more behind in the period

// True if s/2 ticks, at a ticks of time each, and a frame, at d ticks of
// time, take at most part/16 of s/2 ticks; a and d in 1/256 ticks.
static Boolean Fits(UInt16 s, UInt16 part, UInt32 a, UInt32 d)
{
	return (UInt32)s * a * 16 + d * 32 <= (UInt32)s * part * 256;
}

static void PaceSet(UInt8 s, UInt32 busy)
{
#ifdef DEBUG
	if (s != gStride)
		LOG(("stride %u/2, busy %lu half sys ticks for %u ticks, draws %lu for %u", (UInt16)s,
			busy, gPaceTicks, gPaceDraw, gPaceFrames));
#endif
	gStride = s;
	gPaceNew = true;
}

// Sets the stride at the end of a period: up to the least that fits in
// 15/16 of the time, or down to the least that fits in 14/16 if it wasn't
// behind, so it doesn't swap back and forth. When behind (behind set) it
// looks from a quarter period on and only goes up.
static void Pace(UInt16 tps, UInt32 now, Boolean behind)
{
	UInt32 wall, busy, a, d;
	UInt8 s;

	if (behind)
		gPaceBehind = true;
	if (gPaceNew || gPaceTicks < (behind ? kPaceTicks / 4 : kPaceTicks))
		return;
	wall = now - gPaceStart;
	// Longer than 4 times its game time, it had stopped (a form on top).
	if (wall > (UInt32)gPaceTicks * tps * 4 / GAME_TICKS_PER_SECOND) {
		gPaceNew = true;
		return;
	}
	wall = wall > gPaceSkip ? (wall - gPaceSkip) * 2 : 0;
	busy = wall > gPaceIdle ? wall - gPaceIdle : 0;
	// Busy time less drawing a tick, and drawing a frame, in 1/256 ticks
	// (a tick is tps / 25 half sys ticks).
	d = gPaceDraw * 2;
	a = (busy > d ? busy - d : 0) * 25 * 256 / ((UInt32)tps * gPaceTicks);
	d = d * 25 * 256 / ((UInt32)tps * (gPaceFrames ? gPaceFrames : 1));
	for (s = 2; s < kMaxStride && !Fits(s, 15, a, d); s++)
		;
	if (behind) {
		if (s < gStride)
			s = gStride;
	} else if (s < gStride) {
		if (gPaceBehind)
			s = gStride;
		else
			while (s < gStride && !Fits(s, 14, a, d))
				s++;
	}
#ifdef PACE_LOG
	LOG(("pace %u/2 -> %u/2 behind %u/%u wall %lu idle %lu skip %lu draw %lu ticks %u frames %u",
		(UInt16)gStride, (UInt16)s, (UInt16)behind, (UInt16)gPaceBehind, now - gPaceStart,
		gPaceIdle, gPaceSkip, gPaceDraw, gPaceTicks, gPaceFrames));
#endif
	PaceSet(s, busy);
}

// Polls a network game and puts who it waits for over the game. When it
// has stopped, says why and goes to the title, returning false.
static Boolean SEC_COLD NetTurns(void)
{
	Char line[48];

	if (NetPoll() != NET_STOPPED) {
		ViewMessage(NetLine(&gGame, line) ? line : NULL);
		return true;
	}
	StrCopy(line, NetWhy());
	NetEnd();
	gNet = false;
	gFormOpen = false;
	gInGame = false;
	ViewMessage(NULL);
	FrmCustomAlert(ViewAlert, line, "", "");
	FrmGotoForm(TitleForm);
	return false;
}

static void EventLoop(void)
{
	EventType e;
	UInt16 tps = gDev.ticksPerSecond;
#if defined(DEBUG) && defined(TEST_LEAVE_AT)
	Boolean left = false;
#endif
	UInt32 last = TimGetTicks(), now, acc = 0, owed, due, t0, t1;
	UInt16 n;
	Int32 wait;
	Boolean live, run;
#ifdef AUTOQUIT_SECONDS
	// System ticks, which stop with the CPU under a debugger; the clock
	// in seconds runs on.
	UInt32 startTicks = TimGetTicks();
#endif

#ifdef LOOP_PARTS
	{
		UInt32 u = TimingNowUs();
		UInt16 i;

		for (i = 0; i < 100; i++)
			ReadInputs();
		LOG(("100 ReadInputs %lu us", TimingNowUs() - u));
		u = TimingNowUs();
		for (i = 0; i < 100; i++)
			KeysHeld();
		LOG(("100 KeysHeld %lu us", TimingNowUs() - u));
	}
#endif
	// acc counts system ticks times 50, and one game tick costs tps of it.

	while (!gQuit) {
		MARK(LP_OTHER);
#ifdef AUTOQUIT_SECONDS
		if (TimGetTicks() - startTicks >= (UInt32)AUTOQUIT_SECONDS * tps)
			gQuit = true;
#endif
#ifdef DEBUG
		t1 = TimGetTicks();
#endif
		// Wait for an event until the next frame's ticks are due.
		due = (UInt32)((gStride + gStrideOdd) >> 1) * tps;
		wait = 1;
		now = TimGetTicks();
		if (gForm == GameForm && gFormOpen) {
			owed = acc + (now - last) * GAME_TICKS_PER_SECOND;
			wait = owed >= due ? 0 : (Int32)((due - owed + GAME_TICKS_PER_SECOND - 1) /
				GAME_TICKS_PER_SECOND);
		}
		MARK(LP_OTHER);
		EvtGetEvent(&e, wait);
		MARK(wait ? LP_SLEEP : LP_POLL);
		// The wait ends on a system tick and started half way through one,
		// on average.
		if (wait > 0 && (t0 = TimGetTicks() - now) > 0)
			gPaceIdle += t0 * 2 - 1;
#ifdef LOOP_PARTS
		TimingWait(wait, e.eType == nilEvent);
		MARK(LP_DEBUG);
#endif
		HandleEvent(&e);
		MARK(LP_HANDLE);
		// A form or alert came or went: its time isn't owed and a new
		// period starts, so it doesn't count as busy.
		if (e.eType == winEnterEvent || e.eType == winExitEvent) {
			gPaceNew = true;
			acc = 0;
			last = TimGetTicks();
		}
		TimingEvent(TimGetTicks() - t1);
		MARK(LP_DEBUG);

		now = TimGetTicks();
		acc += (now - last) * GAME_TICKS_PER_SECOND;
		last = now;
		// A frame's ticks run together once they are all due.
		live = gForm == GameForm && gFormOpen && !gGame.over;
		if (!live)
			acc = 0;
		run = live && acc >= due;
		if (run && gPaceNew) {
			gPaceNew = false;
			gPaceStart = now;
			gPaceIdle = gPaceDraw = gPaceSkip = 0;
			gPaceTicks = gPaceFrames = 0;
			gPaceBehind = false;
		}
		if (gNet && gInGame && gForm == GameForm && gFormOpen && !NetTurns())
			run = false;
		for (n = 0; run && n < (gStride + gStrideOdd) >> 1; n++) {
			GameInput all[GAME_MAX_TANKS];
			UInt8 vt = game_viewer_tank(&gGame);
			GameInput in;

			if (gNet && !NetReady())
				break;
			in = ReadInputs();

			// Bot tanks ignore their entry; the rest have nobody at the keys.
			MemSet(all, sizeof(all), 0);
			all[vt] = in;
			MARK(LP_INPUT);
#ifdef DEBUG
			t1 = TimGetTicks();
#endif
			if (gNet)
				NetTick(&gGame, &in);
			else
				game_tick(&gGame, all);
#ifdef DEBUG
			MARK(LP_TICK);
			TimingTick(TimGetTicks() - t1);
#endif
#if defined(DEBUG) && !defined(QUIET_TICKS)
			if (gGame.tick % 50 == 0) {
				const Tank *k = &gGame.tanks[vt];

				UInt16 own = 0, pillShells = 0, i;

				for (i = 0; i < gGame.map.numBases; i++)
					own += gGame.map.bases[i].owner == k->player;
				for (i = 0; i < gGame.numShells; i++)
					pillShells += gGame.shells[i].owner == GAME_NO_PLAYER;
				LOG(("tick %lu keys %x tank %u,%u ang %u spd %d A%u S%u M%u dead %u/%u boat %u "
					"P%u B%u pillshells %u",
					gGame.tick, (UInt16)in.keys, k->x, k->y, k->angle, k->speed,
					(UInt16)k->armour, (UInt16)k->shells, (UInt16)k->mines,
					(UInt16)k->destroyed, k->deaths, (UInt16)k->boat,
					(UInt16)k->numCarry, own, pillShells));
				{
					const Builder *m = &gGame.men[vt];

					LOG(("trees %u man %u,%u state %u action %u in %u dead %u",
						(UInt16)k->trees, m->x, m->y, (UInt16)m->state, (UInt16)m->action,
						(UInt16)m->inTank, (UInt16)m->isDead));
				}
			}
			MARK(LP_TICKLOG);
#endif
			acc -= tps;
		}
		if (gNet && gInGame && gForm == GameForm && gFormOpen)
			NetTurns();
		t1 = TimGetTicks();
		gPaceTicks += n;
		if (n)
			gStrideOdd = (gStride + gStrideOdd) & 1;
		// At most two frames' or 10 ticks' time stays owed, whichever is
		// more. Owing 5 ticks after a frame's ticks steps the stride up
		// before time is dropped, unless it's from a full repaint or a debug
		// log less than 16 ticks ago and not caught up yet.
		owed = 2 * due > 10 * (UInt32)tps ? 2 * due : 10 * (UInt32)tps;
		if (n && !gPaceSlow && acc > 5 * (UInt32)tps)
			Pace(tps, t1, true);
		if (acc > owed) {
			acc = owed;
#ifdef DEBUG
			if (live)
				gDropped++;
#endif
		}
		gPaceSlow = acc < due || gPaceSlow < n ? 0 : gPaceSlow - n;
#if defined(DEBUG) && defined(TEST_LEAVE_AT)
		if (!left && gInGame && gForm == GameForm && gGame.tick >= TEST_LEAVE_AT) {
			left = true;
			LeaveGame();
		}
#endif
		// The time limit ran out: show who did what.
		if (gInGame && gForm == GameForm && gGame.over) {
			gInGame = false;
			gFormOpen = false;
			if (gNet) {
				NetFinish(&gGame);
				ViewMessage(NULL);
			} else {
				SaveDrop();
			}
			FrontResults(&gGame, gNet);
			gNet = false;
		}
		MARK(LP_OTHER);
		if (gForm == GameForm && gFormOpen)
			SoundFrame(&gGame);
		MARK(LP_SOUND);

		// A menu or alert is on top, so leave the screen alone.
		if (gForm == GameForm && gFormOpen && !gQuit && !gMenuOpen &&
		    WinGetActiveWindow() == FrmGetWindowHandle(FrmGetActiveForm())) {
			Boolean full = gFull;

			gFull = gDirty = false;
			MARK(LP_OTHER);
			ViewFrame(full, n != 0);
			NetService();
			MARK(LP_DEBUG);
			// Full repaints are rare and slow, so they stay out of the pacing.
			if (full) {
				gPaceSkip += TimGetTicks() - t1;
				gPaceSlow = kPaceTicks / 4;
			} else if (n) {
				gPaceDraw += TimGetTicks() - t1;
				gPaceFrames++;
			}
			if (full || n)
				TimingFrame(n);
			MARK(LP_DEBUG);
#ifdef DEBUG
			if (full && !gTimedFull) {
				gTimedFull = true;
				LOG(("start: first frame %lu sys ticks after Start", TimGetTicks() - gStartTicks));
				TimingHeap("first frame");
#ifdef TIME_FULL
				ViewTimeFull(8);
#endif
#ifdef DRAW_BENCH
				DrawBench();
#endif
#ifdef SCROLL_BENCH
				ViewScrollBench();
#endif
			}
#endif
		}
		Pace(tps, TimGetTicks(), false);
		MARK(LP_OTHER);
#ifdef DEBUG
		// Logging every 5 s, kept out of the times it reports and the pacing.
		if (TimingDue()) {
			t1 = TimGetTicks();
			if (gDropped) {
				LOG(("fell behind: ticks dropped %u times", gDropped));
				gDropped = 0;
			}
			TimingMaybeLog();
#ifdef TICK_PARTS
			TickPartsLog();
#endif
			gPaceSkip += TimGetTicks() - t1;
			gPaceSlow = kPaceTicks / 4;
		}
		MARK(LP_LOG);
#endif
	}
}

#ifdef DRAW_BENCH
// Debug: 200 frames of 5 ticks each from a fixed seed and fixed keys, the
// same on every run, timing the drawing and the ticks apart.
static void SEC_COLD DrawBench(void)
{
	UInt32 draw = 0, ticks = 0, t0;
	UInt16 f, t;

	for (f = 0; f < 200 && !gGame.over; f++) {
		for (t = 0; t < 5; t++) {
			GameInput all[GAME_MAX_TANKS];

			MemSet(all, sizeof(all), 0);
#ifndef BENCH_STILL
			all[game_viewer_tank(&gGame)].keys = (UInt8)(IN_FASTER | (f % 40 < 6 ? IN_LEFT : 0) |
				(f % 10 == 0 && t == 0 ? IN_FIRE : 0));
#endif
			t0 = TimGetTicks();
			game_tick(&gGame, all);
			ticks += TimGetTicks() - t0;
		}
		t0 = TimGetTicks();
		ViewFrame(false, true);
		draw += TimGetTicks() - t0;
	}
	LOG(("draw bench check: %u view bytes differ from a full redraw", ViewCheck()));
#ifdef TICK_PARTS
	TickPartsLog();
#endif
	LOG(("draw bench %u frames x5 ticks: draw %lu sys ticks (%lu ms a frame), ticks %lu sys ticks",
		f, draw, draw * 10 / f, ticks));
}
#endif

static void SEC_COLD AppStart(void)
{
	DeviceDetect(&gDev);
#ifdef DEBUG
	LogClear();
	LOG(("PalmBolo %s debug start", kAppVersion));
	LogDevice(&gDev);
#ifdef SELFTEST_AT_START
	SelfTest();
#endif
#endif
	TimingInit(gDev.ticksPerSecond);
#ifdef TICK_PARTS
	TickPartInit();
#endif
	LoadPrefs();
	ScreenInit(&gDev);
	ViewPins(gDev.pins);
	gQuit = gFormOpen = false;
	gDirty = gFull = true;
	FrmGotoForm(TitleForm);
}

static void SEC_COLD AppStop(void)
{
	// Leaving the app in the middle of a game saves it, unless it's a
	// network game.
	if (gNet) {
		gInGame = false;
		gNet = false;
	}
	NetEnd();
	if (gInGame && !gGame.over) {
		gInGame = false;
		if (!SaveWrite(&gGame))
			FrmAlert(SaveFailAlert);
	}
	SoundStop();
	KeysStop();
	ViewClose();
	FrmCloseAllForms();
	ScreenExit();
	LOG(("stop tick %lu", gGame.tick));
}

UInt32 PilotMain(UInt16 cmd, void *cmdPBP, UInt16 launchFlags)
{
	UInt32 rom = 0;

	if (cmd != sysAppLaunchCmdNormalLaunch)
		return 0;
	FtrGet(sysFtrCreator, sysFtrNumROMVersion, &rom);
	if (rom < kMinRomVersion) {
		FrmAlert(RomIncompatibleAlert);
		return sysErrRomIncompatible;
	}
	AppStart();
	EventLoop();
	AppStop();
	return 0;
}
