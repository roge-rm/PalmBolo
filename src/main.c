#include "palmbolo.h"
#include "resource.h"
#include "game/game.h"
#include "game/script.h"
#include "game/util.h"

static DeviceInfo gDev;
static GameState gGame;
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

static void Defaults(void)
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

static void LoadPrefs(void)
{
	UInt16 size = sizeof(gPrefs);
	Int16 v;

	v = PrefGetAppPreferences(kAppCreator, kPrefsId, &gPrefs, &size, true);
	if (v != kPrefsVersion || size != sizeof(gPrefs))
		Defaults();
	SoundSetOn(gPrefs.sound != 0);
}

void AppPrefsSave(void)
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

static void ShowAbout(void)
{
	FrmCustomAlert(AboutAlert, kAppVersion, "", "");
	gDirty = gFull = true;
}

#ifdef DEBUG
static void SelfTest(void);
#endif

// Leaves a game in progress for the title, saving it unless it is over.
static void LeaveGame(void)
{
	if (gInGame && gForm == GameForm) {
		if (!gGame.over && !SaveWrite(&gGame)) {
			// Stay in the game, it is untouched.
			FrmAlert(SaveFailAlert);
			return;
		}
		gFormOpen = false;
		gInGame = false;
		FrmGotoForm(TitleForm);
	}
}

static Boolean HandleMenu(UInt16 id)
{
	switch (id) {
	case MainMenuAbout:
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

void AppOpenMap(UInt16 index, const Char *name)
{
	Int16 err = MapLoad(index, name, &gGame);

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
		game_init(&gGame, TimGetSeconds(), &setup);
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

void AppResume(void)
{
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
		if (!ViewOpen(&gDev, &gGame)) {
			gInGame = false;
			FrmGotoForm(TitleForm);
			return true;
		}
		SoundOpen(gDev.ticksPerSecond);
		gFormOpen = true;
		gDirty = gFull = true;
		return true;
	case frmCloseEvent:
		gFormOpen = false;
		SoundStop();
		ViewClose();
		return false;
	default:
		break;
	}
	return false;
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
		if (IsGameKey(c) || c == vchrTxCenter)
			return true;
	}
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
	if (SysHandleEvent(e))
		return true;
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
		break;
	case menuEvent:
		return HandleMenu(e->data.menu.itemID);
	case winEnterEvent:
	case winExitEvent:
		gDirty = gFull = true;
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
	if ((k & kKeyNavSelect) || gFire) {
		in.keys |= IN_FIRE;
		if (gFire)
			gFire--;
	}
	if (gSight > 0)
		in.keys |= IN_SIGHT_FAR;
	else if (gSight < 0)
		in.keys |= IN_SIGHT_NEAR;
	gSight = 0;
	in.build = gBuild;
	in.buildX = gBuildX;
	in.buildY = gBuildY;
	gBuild = BUILD_NONE;
	return in;
}

#ifdef DEBUG
// Times the bots apart from the rules over BENCH_TICKS ticks of the game
// the script left: the sys ticks seen to pass while the bots make their
// keys add up to their share, on average, though each call is much shorter
// than a sys tick.
#define BENCH_TICKS 1000
static void BenchBots(void)
{
	GameState *c = (GameState *)MemPtrNew(sizeof(GameState));
	GameInput none[GAME_MAX_TANKS], keys[GAME_MAX_TANKS];
	UInt32 t0, t1, a, bots = 0;
	UInt16 i, n = 0;
	UInt8 mask;

	if (!c)
		return;
	MemSet(none, sizeof(none), 0);
	MemMove(c, &gGame, sizeof(GameState));
	mask = c->botMask;
	for (i = 0; i < GAME_MAX_TANKS; i++)
		n += (mask >> i) & 1;
	t0 = TimGetTicks();
	for (i = 0; i < BENCH_TICKS; i++) {
		a = TimGetTicks();
		game_bot_inputs(c, none, keys);
		bots += TimGetTicks() - a;
		c->botMask = 0;
		game_tick(c, keys);
		c->botMask = mask;
	}
	t1 = TimGetTicks();
	LOG(("bench %d tanks %d bots x%d: %lu sys ticks, bots %lu", (Int16)c->numTanks, (Int16)n, BENCH_TICKS,
		t1 - t0, bots));
	MemPtrFree(c);
}

// Checks the const tables read back right from both code segments, then
// runs the scripted games on Everard Island and logs their checksums for
// comparing with the host test.
static void SelfTest(void)
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
	for (n = 0; n < SCRIPT_COUNT; n++) {
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

static void EventLoop(void)
{
	EventType e;
	UInt16 tps = gDev.ticksPerSecond;
#if defined(DEBUG) && defined(TEST_LEAVE_AT)
	Boolean left = false;
#endif
	UInt32 last = TimGetTicks(), now, acc = 0, t0, t1;
	UInt16 n;
#ifdef AUTOQUIT_SECONDS
	UInt32 startSeconds = TimGetSeconds();
#endif

	// The game runs at 50 ticks a second. acc counts system ticks times 50,
	// and one game tick costs tps of it. After a long stall at most
	// kMaxCatchUp ticks are run and the rest is dropped.
	const UInt16 kMaxCatchUp = 10;

	while (!gQuit) {
#ifdef AUTOQUIT_SECONDS
		if (TimGetSeconds() - startSeconds >= AUTOQUIT_SECONDS)
			gQuit = true;
#endif
		EvtGetEvent(&e, 1);
		HandleEvent(&e);

		now = TimGetTicks();
		acc += (now - last) * GAME_TICKS_PER_SECOND;
		last = now;
		n = 0;
		t0 = now;
		while (acc >= tps && n < kMaxCatchUp && gForm == GameForm && gFormOpen && !gGame.over) {
			GameInput all[GAME_MAX_TANKS];
			UInt8 vt = game_viewer_tank(&gGame);
			GameInput in = ReadInputs();

			// Bot tanks ignore their entry; the rest have nobody at the keys.
			MemSet(all, sizeof(all), 0);
			all[vt] = in;
			game_tick(&gGame, all);
#ifdef DEBUG
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
#endif
			acc -= tps;
			n++;
		}
		if (n == kMaxCatchUp)
			acc = 0;
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
			SaveDrop();
			FrontResults(&gGame);
		}
		if (gForm == GameForm && gFormOpen)
			SoundFrame(&gGame);
		if (n) {
			t1 = TimGetTicks();
			TimingTicks(t0, t1, n);
		}

		// A menu or alert is on top, so leave the screen alone.
		if (gForm == GameForm && gFormOpen && !gQuit && !gMenuOpen &&
		    WinGetActiveWindow() == FrmGetWindowHandle(FrmGetActiveForm())) {
			Boolean full = gFull;

			gFull = gDirty = false;
			ViewFrame(full, n != 0);
#ifdef DEBUG
			if (full && !gTimedFull) {
				gTimedFull = true;
				ViewTimeFull(8);
			}
#endif
		}
		TimingMaybeLog();
	}
}

static void AppStart(void)
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
	LoadPrefs();
	ScreenInit(&gDev);
	ViewPins(gDev.pins);
	gQuit = gFormOpen = false;
	gDirty = gFull = true;
	FrmGotoForm(TitleForm);
}

static void AppStop(void)
{
	// Leaving the app in the middle of a game saves it.
	if (gInGame && !gGame.over) {
		gInGame = false;
		if (!SaveWrite(&gGame))
			FrmAlert(SaveFailAlert);
	}
	SoundStop();
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
