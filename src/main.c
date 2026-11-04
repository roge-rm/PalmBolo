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
static Char gNames[kMaxMaps][kMapNameLen];
static Char *gNamePtrs[kMaxMaps];
static UInt16 gNumMaps;
static UInt8 gFire;         // ticks of fire left from a 5-way centre press
static Int8 gSight;         // gunsight steps asked for by taps, sent next tick
#ifdef DEBUG
static Boolean gTimedFull;
#endif

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
	FrmAlert(AboutAlert);
	gDirty = gFull = true;
}

#ifdef DEBUG
static void SelfTest(void);
#endif

static Boolean HandleMenu(UInt16 id)
{
	switch (id) {
	case MainMenuAbout:
		ShowAbout();
		return true;
	case MainMenuMaps:
		if (gForm == GameForm)
			FrmGotoForm(MapListForm);
		return true;
#ifdef DEBUG
	case MainMenuSelfTest:
		SelfTest();
		FrmGotoForm(MapListForm);
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
	game_init(&gGame, TimGetSeconds(), 0);
#if defined(DEBUG) && defined(DEMO_START)
	// For replays: the tank on land six squares west of the first base,
	// facing it.
	if (gGame.map.numBases > 0) {
		Tank *k = &gGame.tanks[0];

		k->x = (UInt16)(((UInt16)gGame.map.bases[0].x - 6) * 256 + 128);
		k->y = (UInt16)((UInt16)gGame.map.bases[0].y * 256 + 128);
		k->angle = 64 * ANGLE_ONE;
		k->boat = BOAT_OFF;
	}
#endif
	FrmGotoForm(GameForm);
}

static Boolean MapListHandler(EventType *e)
{
	FormType *frm = FrmGetActiveForm();

	switch (e->eType) {
	case frmOpenEvent: {
		ListType *lst = FrmGetObjectPtr(frm, FrmGetObjectIndex(frm, MapList));
		UInt16 i;

		gNumMaps = MapListBuild(gNames);
		for (i = 0; i < gNumMaps; i++)
			gNamePtrs[i] = gNames[i];
		LstSetListChoices(lst, gNamePtrs, gNumMaps);
		LstSetSelection(lst, 0);
		FrmDrawForm(frm);
		gFormOpen = true;
		return true;
	}
	case lstSelectEvent:
		if (e->data.lstSelect.selection < gNumMaps)
			AppOpenMap(e->data.lstSelect.selection,
				gNames[e->data.lstSelect.selection]);
		return true;
	case frmCloseEvent:
		gFormOpen = false;
		return false;
	default:
		break;
	}
	return false;
}

static Boolean GameHandler(EventType *e)
{
	switch (e->eType) {
	case frmOpenEvent:
		FrmDrawForm(FrmGetActiveForm());
		ViewOpen(&gDev, &gGame);
		gFormOpen = true;
		gDirty = gFull = true;
		return true;
	case frmCloseEvent:
		gFormOpen = false;
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
	// A tap on the map moves the gunsight: above the middle further, below
	// nearer.
	if (gForm == GameForm && !gMenuOpen && e->eType == penDownEvent &&
	    e->screenY >= 0) {
		Coord w, h;

		WinGetDisplayExtent(&w, &h);
		gSight = e->screenY < h / 2 ? 1 : -1;
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
		FrmSetEventHandler(frm, gForm == GameForm ? GameHandler : MapListHandler);
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

// Held hardware buttons as game inputs. Turn with the 5-way or the
// outer buttons, speed up and slow down with up and down, fire with the
// centre or the second button, lay a mine with the third. A tap above the
// middle of the screen moves the gunsight further, below it nearer.
#define kKeyNavLeft     0x01000000UL
#define kKeyNavRight    0x02000000UL
#define kKeyNavSelect   0x04000000UL

static GameInput ReadInputs(void)
{
	UInt32 k = KeyCurrentState();
	GameInput in;

	in.keys = 0;
	in.opts = OPT_AUTOSLOW;
	if (k & (keyBitHard1 | kKeyNavLeft))
		in.keys |= IN_LEFT;
	if (k & (keyBitHard4 | kKeyNavRight))
		in.keys |= IN_RIGHT;
	if (k & keyBitPageUp)
		in.keys |= IN_FASTER;
	if (k & keyBitPageDown)
		in.keys |= IN_SLOWER;
	if ((k & (keyBitHard2 | kKeyNavSelect)) || gFire) {
		in.keys |= IN_FIRE;
		if (gFire)
			gFire--;
	}
	if (k & keyBitHard3)
		in.keys |= IN_MINE;
	if (gSight > 0)
		in.keys |= IN_SIGHT_FAR;
	else if (gSight < 0)
		in.keys |= IN_SIGHT_NEAR;
	gSight = 0;
	return in;
}

#ifdef DEBUG
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
	}
}
#endif

static void EventLoop(void)
{
	EventType e;
	UInt16 tps = gDev.ticksPerSecond;
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
		while (acc >= tps && n < kMaxCatchUp && gForm == GameForm && gFormOpen) {
			GameInput in = ReadInputs();

			game_tick(&gGame, &in);
#ifdef DEBUG
			if (gGame.tick % 50 == 0) {
				const Tank *k = &gGame.tanks[0];

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
			}
#endif
			acc -= tps;
			n++;
		}
		if (n == kMaxCatchUp)
			acc = 0;
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
	LOG(("PalmBolo 0.1 debug start"));
	LogDevice(&gDev);
#ifdef SELFTEST_AT_START
	SelfTest();
#endif
#endif
	TimingInit(gDev.ticksPerSecond);
	ScreenInit(&gDev);
	gQuit = gFormOpen = false;
	gDirty = gFull = true;
	FrmGotoForm(MapListForm);
}

static void AppStop(void)
{
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
