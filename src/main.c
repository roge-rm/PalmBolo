#include "palmbolo.h"
#include "resource.h"
#include "game/game.h"

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
#ifdef DEBUG
static Boolean gTimedFull;
#endif

// Hardware buttons the game uses. They are consumed so they don't launch
// other applications.
#define vchrNavChange   0x0503   // 5-way on palmOne devices
#define navBitSelect    0x0010

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
		// The 5-way centre goes back to the map list.
		if (gForm == GameForm && (c == vchrRockerCenter ||
		    (c == vchrNavChange && (e->data.keyDown.keyCode & navBitSelect)))) {
			FrmGotoForm(MapListForm);
			return true;
		}
		if (IsGameKey(c))
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

// Held hardware buttons as game inputs.
static uint16_t ReadInputs(void)
{
	return (uint16_t)(KeyCurrentState() & 0xFFFF);
}

#ifdef DEBUG
static void SelfTest(void)
{
	Char hex[9];

	HexStr(game_selftest(), hex);
	LOG(("selftest seed 12345 ticks 1000 checksum %s", hex));
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
		while (acc >= tps && n < kMaxCatchUp) {
			game_tick(&gGame, ReadInputs());
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
			ViewFrame(full);
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
	SelfTest();
#endif
	TimingInit(gDev.ticksPerSecond);
	ScreenInit(&gDev);
	game_init(&gGame, 12345);
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
