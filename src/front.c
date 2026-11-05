// The screens around the game: title, new game (map list and settings),
// the player's settings and the results, and the saved game. All in a code
// segment of their own, and the title picture is loaded only while the
// title is on screen.
#include "palmbolo.h"
#include "resource.h"
#include "art.h"
#include "game/game.h"
#include "game/mapstore.h"
#include "game/util.h"

#define kSaveDbName     "PalmBoloSavedGame"
#define kSaveDbType     'Sgam'
#define kSaveCreator    'Rmrf'
#define kSaveMagic      0x50426F53UL    // "PBoS"
#define kSaveVersion    1

typedef struct SaveHead {
	UInt32 magic;
	UInt16 version;
	UInt16 size;        // sizeof(GameState)
} SaveHead;

#define kTmpDbName      "PalmBoloSavingGame"
#define kTmpDbType      'Stmp'

static void SEC_FRONT DeleteTmpDb(void)
{
	LocalID id = DmFindDatabase(0, kTmpDbName);

	if (id)
		DmDeleteDatabase(0, id);
}

static void SEC_FRONT DeleteStateDb(void)
{
	LocalID id = DmFindDatabase(0, kSaveDbName);

	if (id)
		DmDeleteDatabase(0, id);
}

// Opens the state database, or NULL when it is missing or from another build.
static DmOpenRef SEC_FRONT OpenState(void)
{
	DmOpenRef db = DmOpenDatabaseByTypeCreator(kSaveDbType, kSaveCreator, dmModeReadOnly);
	MemHandle h;

	if (!db)
		return NULL;
	h = DmQueryRecord(db, 0);
	if (DmNumRecords(db) == 1 && h && MemHandleSize(h) == sizeof(SaveHead) + sizeof(GameState)) {
		const SaveHead *sh = (const SaveHead *)MemHandleLock(h);
		Boolean ok = sh->magic == kSaveMagic && sh->version == kSaveVersion &&
			sh->size == sizeof(GameState);

		MemHandleUnlock(h);
		if (ok)
			return db;
	}
	DmCloseDatabase(db);
	return NULL;
}

Boolean SEC_FRONT SaveExists(void)
{
	DmOpenRef db;

	if (!mapstore_have_save())
		return false;
	db = OpenState();
	if (!db)
		return false;
	DmCloseDatabase(db);
	return true;
}

// The state is written to a database of another type first, then the map is
// saved, then the state replaces the old one. A failure before the map is
// saved leaves the live game and the old save as they were.
Boolean SEC_FRONT SaveWrite(const GameState *g)
{
	DmOpenRef db;
	MemHandle h;
	LocalID id;
	UInt16 index = dmMaxRecordIndex;
	UInt32 type = kSaveDbType;
	SaveHead sh;
	UInt8 *rec;

	DeleteTmpDb();
	if (DmCreateDatabase(0, kTmpDbName, kSaveCreator, kTmpDbType, false) != errNone)
		return false;
	db = DmOpenDatabaseByTypeCreator(kTmpDbType, kSaveCreator, dmModeReadWrite);
	if (!db) {
		DeleteTmpDb();
		return false;
	}
	h = DmNewRecord(db, &index, sizeof(sh) + sizeof(GameState));
	if (!h) {
		DmCloseDatabase(db);
		DeleteTmpDb();
		return false;
	}
	sh.magic = kSaveMagic;
	sh.version = kSaveVersion;
	sh.size = sizeof(GameState);
	rec = (UInt8 *)MemHandleLock(h);
	DmWrite(rec, 0, &sh, sizeof(sh));
	DmWrite(rec, sizeof(sh), g, sizeof(GameState));
	MemHandleUnlock(h);
	DmReleaseRecord(db, index, true);
	DmCloseDatabase(db);
	if (mapstore_save() != 0) {
		DeleteTmpDb();
		return false;
	}
	DeleteStateDb();
	id = DmFindDatabase(0, kTmpDbName);
	if (!id || DmSetDatabaseInfo(0, id, kSaveDbName, NULL, NULL, NULL, NULL, NULL, NULL,
	        NULL, NULL, &type, NULL) != errNone) {
		// Without its state the saved map is of no use.
		DeleteTmpDb();
		mapstore_drop_save();
		return false;
	}
	return true;
}

Boolean SEC_FRONT SaveRead(GameState *g)
{
	DmOpenRef db = OpenState();
	MemHandle h;

	if (!db)
		return false;
	h = DmQueryRecord(db, 0);
	MemMove(g, (const UInt8 *)MemHandleLock(h) + sizeof(SaveHead), sizeof(GameState));
	MemHandleUnlock(h);
	DmCloseDatabase(db);
	return mapstore_resume() == 0;
}

void SEC_FRONT SaveDrop(void)
{
	DeleteTmpDb();
	DeleteStateDb();
	mapstore_drop_save();
}

// ---- helpers ----

// Makes the form as big as the screen, so it covers what was there.
static void SEC_FRONT FitWindow(FormType *frm)
{
	RectangleType r;

	if (ScreenNewWin()) {
		ScreenDisplayRect(&r);
		WinSetBounds(FrmGetWindowHandle(frm), &r);
	}
}

// A new form: the input area is closed for a bigger picture, open to type
// a name.
static void SEC_FRONT FitForm(FormType *frm, Boolean inputArea)
{
	ViewFormInputArea(!inputArea);
	FitWindow(frm);
}

static void SEC_FRONT Go(UInt16 form)
{
	FrmGotoForm(form);
}

static void * SEC_FRONT Obj(FormType *frm, UInt16 id)
{
	return FrmGetObjectPtr(frm, FrmGetObjectIndex(frm, id));
}

// ---- tall screens ----

static Coord gMidShift;     // how far the form's contents have moved down
static Coord gBotShift;     // and its bottom buttons

// The form's 160 high layout on a taller screen. Everything but the title
// bar and the two bottom buttons moves down half the extra height, the
// buttons go to the bottom. With a list id given, the contents stay and that
// list gets the extra rows instead. reset is true on a freshly loaded form.
static void SEC_FRONT TallLayout(FormType *frm, UInt16 bot1, UInt16 bot2,
	UInt16 list, Boolean reset)
{
	RectangleType r;
	Coord extra, mid, bot, dm, db;
	UInt16 i, n;

	if (reset)
		gMidShift = gBotShift = 0;
	ScreenDisplayRect(&r);
	extra = (Coord)(r.extent.y - 160);
	if (extra < 0)
		extra = 0;
	bot = extra;
	mid = list != 0xFFFF ? 0 : (Coord)(extra / 2);
	dm = (Coord)(mid - gMidShift);
	db = (Coord)(bot - gBotShift);
	n = FrmGetNumberOfObjects(frm);
	for (i = 0; i < n; i++) {
		UInt16 id = FrmGetObjectId(frm, i);
		FormObjectKind kind = FrmGetObjectType(frm, i);
		Coord x, y;

		if (kind == frmTitleObj || kind == frmGraffitiStateObj || kind == frmPopupObj)
			continue;
		if (id == list) {
			LstSetHeight((ListType *)FrmGetObjectPtr(frm, i), (Int16)(11 + extra / 11));
			continue;
		}
		FrmGetObjectPosition(frm, i, &x, &y);
		if (kind == frmListObj) {
			LstSetPosition((ListType *)FrmGetObjectPtr(frm, i), x,
				(Coord)(y + (id == bot1 || id == bot2 ? db : dm)));
			continue;
		}
		if (id == bot1 || id == bot2) {
			if (db)
				FrmSetObjectPosition(frm, i, x, (Coord)(y + db));
		} else if (dm) {
			FrmSetObjectPosition(frm, i, x, (Coord)(y + dm));
		}
	}
	gMidShift = mid;
	gBotShift = bot;
}

// ---- title ----

static MemHandle gTitleRes;     // the picture, locked while the title shows

static void SEC_FRONT TitleLayout(FormType *frm, Boolean haveSave)
{
	UInt16 ids[4];
	RectangleType r;
	Coord h;
	UInt16 i, n = 0;

	ids[0] = TitleResume;
	ids[1] = TitleNew;
	ids[2] = TitleJoin;
	ids[3] = TitleSettings;
	ScreenDisplayRect(&r);
	h = r.extent.y;
	for (i = 0; i < 4; i++) {
		UInt16 idx = FrmGetObjectIndex(frm, ids[i]);
		Coord x, y;

		if (i == 0 && !haveSave) {
			FrmHideObject(frm, idx);
			continue;
		}
		if (h <= 170) {
			x = (Coord)(n % 2 ? 82 : 4);
			y = (Coord)(113 + (n / 2) * 17);
		} else {
			x = 43;
			y = (Coord)(116 + n * 20);
		}
		FrmSetObjectPosition(frm, idx, x, y);
		n++;
	}
}

// The picture is 160 tall in standard coordinates with the bottom 50 rows
// plain; only the top part is drawn so the buttons stay on top.
static void SEC_FRONT TitleDraw(FormType *frm)
{
	RectangleType r, clip;
	Char ver[12];
	FontID old;

	FrmDrawForm(frm);
	if (!gTitleRes)
		gTitleRes = DmGetResource(bitmapRsc, ArtTitleBmp);
	if (gTitleRes) {
		clip.topLeft.x = 0;
		clip.topLeft.y = 0;
		clip.extent.x = 160;
		clip.extent.y = 111;
		WinSetClip(&clip);
		WinDrawBitmap((BitmapType *)MemHandleLock(gTitleRes), 0, 0);
		MemHandleUnlock(gTitleRes);
		WinResetClip();
	}
	// The version, small, in the bottom right corner.
	ScreenDisplayRect(&r);
	StrCopy(ver, kAppVersion);
	old = FntSetFont(stdFont);
	WinDrawChars(ver, StrLen(ver), (Coord)(158 - FntCharsWidth(ver, StrLen(ver))),
		(Coord)(r.extent.y - FntLineHeight() - 1));
	FntSetFont(old);
}

static Boolean SEC_FRONT TitleHandler(EventType *e)
{
	FormType *frm = FrmGetActiveForm();

	switch (e->eType) {
	case frmOpenEvent:
		FitForm(frm, false);
		TitleLayout(frm, SaveExists());
		CtlSetEnabled(Obj(frm, TitleJoin), false);
		TitleDraw(frm);
		return true;
	case winDisplayChangedEvent:
		// The input area opened or closed: fit the new size.
		FitWindow(frm);
		TitleLayout(frm, SaveExists());
		TitleDraw(frm);
		return true;
	case frmCloseEvent:
		if (gTitleRes) {
			DmReleaseResource(gTitleRes);
			gTitleRes = NULL;
		}
		return false;
	case penDownEvent: {
		// A disabled button gets no taps, so look for one on Join game.
		RectangleType b;

		FrmGetObjectBounds(frm, FrmGetObjectIndex(frm, TitleJoin), &b);
		if (RctPtInRectangle(e->screenX, e->screenY, &b)) {
			FrmAlert(JoinAlert);
			return true;
		}
		return false;
	}
	case ctlSelectEvent:
		switch (e->data.ctlSelect.controlID) {
		case TitleResume:
			AppResume();
			return true;
		case TitleNew:
			if (SaveExists() && FrmAlert(ReplaceAlert) != 0)
				return true;
			Go(MapListForm);
			return true;
		case TitleSettings:
			Go(SettingsForm);
			return true;
		}
		return false;
	default:
		break;
	}
	return false;
}

// ---- new game ----

static Char gNames[kMaxMaps][kMapNameLen];
static Char *gNamePtrs[kMaxMaps];
static UInt16 gNumMaps;
static UInt16 gMapIndex;        // chosen in the list

static Boolean SEC_FRONT MapListHandler(EventType *e)
{
	FormType *frm = FrmGetActiveForm();
	ListType *lst;

	switch (e->eType) {
	case frmOpenEvent: {
		UInt16 i, sel = 0;
		const AppPrefs *p = AppPrefsGet();

		FitForm(frm, false);
		lst = (ListType *)Obj(frm, MapList);
		gNumMaps = MapListBuild(gNames);
		for (i = 0; i < gNumMaps; i++) {
			gNamePtrs[i] = gNames[i];
			if (StrCompare(gNames[i], p->map) == 0)
				sel = i;
		}
		LstSetListChoices(lst, gNamePtrs, gNumMaps);
		TallLayout(frm, MapNext, MapBack, MapList, true);
		LstSetSelection(lst, sel);
		FrmDrawForm(frm);
		return true;
	}
	case ctlSelectEvent:
		if (e->data.ctlSelect.controlID == MapNext) {
			Int16 sel;

			lst = (ListType *)Obj(frm, MapList);
			sel = LstGetSelection(lst);
			if (sel < 0 || sel >= (Int16)gNumMaps)
				return true;
			gMapIndex = (UInt16)sel;
			StrCopy(AppPrefsGet()->map, gNames[sel]);
			Go(NewGameForm);
			return true;
		}
		if (e->data.ctlSelect.controlID == MapBack) {
			Go(TitleForm);
			return true;
		}
		return false;
	case winDisplayChangedEvent:
		FitWindow(frm);
		TallLayout(frm, MapNext, MapBack, MapList, false);
		FrmDrawForm(frm);
		return true;
	default:
		break;
	}
	return false;
}

// A popup trigger shows the item chosen in its list.
static void SEC_FRONT SetPopup(FormType *frm, UInt16 trig, UInt16 list, UInt8 sel)
{
	ListType *lst = (ListType *)Obj(frm, list);

	LstSetSelection(lst, sel);
	CtlSetLabel((ControlType *)Obj(frm, trig), LstGetSelectionText(lst, sel));
}

static Boolean SEC_FRONT NewGameHandler(EventType *e)
{
	FormType *frm = FrmGetActiveForm();
	AppPrefs *p = AppPrefsGet();

	switch (e->eType) {
	case frmOpenEvent:
		FitForm(frm, false);
		TallLayout(frm, NgStart, NgBack, 0xFFFF, true);
		SetPopup(frm, NgTypeTrig, NgTypeList, p->gameType);
		CtlSetValue((ControlType *)Obj(frm, NgMines), p->hiddenMines);
		SetPopup(frm, NgTimeTrig, NgTimeList, p->timeLimit);
		SetPopup(frm, NgBotsTrig, NgBotsList, p->numBots);
		SetPopup(frm, NgLevelTrig, NgLevelList, p->botLevel);
		SetPopup(frm, NgTeamsTrig, NgTeamsList, p->teams);
		FrmDrawForm(frm);
		return true;
	case popSelectEvent: {
		UInt8 v = (UInt8)e->data.popSelect.selection;

		switch (e->data.popSelect.listID) {
		case NgTypeList: p->gameType = v; break;
		case NgTimeList: p->timeLimit = v; break;
		case NgBotsList: p->numBots = v; break;
		case NgLevelList: p->botLevel = v; break;
		case NgTeamsList: p->teams = v; break;
		}
		return false;   // the form sets the trigger's label
	}
	case ctlSelectEvent:
		if (e->data.ctlSelect.controlID == NgStart) {
			p->hiddenMines = (UInt8)CtlGetValue((ControlType *)Obj(frm, NgMines));
			AppPrefsSave();
			AppOpenMap(gMapIndex, gNames[gMapIndex]);
			return true;
		}
		if (e->data.ctlSelect.controlID == NgBack) {
			p->hiddenMines = (UInt8)CtlGetValue((ControlType *)Obj(frm, NgMines));
			AppPrefsSave();
			Go(MapListForm);
			return true;
		}
		return false;
	case winDisplayChangedEvent:
		FitWindow(frm);
		TallLayout(frm, NgStart, NgBack, 0xFFFF, false);
		FrmDrawForm(frm);
		return true;
	default:
		break;
	}
	return false;
}

// ---- settings ----

static Char gKeyLabel[4][14];
static UInt8 gKeys[4];      // the choices while the form is open

static const Char * SEC_FRONT KeyName(UInt8 k)
{
	switch (k) {
	case KEY_LEFT: return "Left";
	case KEY_RIGHT: return "Right";
	case KEY_FIRE: return "Fire";
	case KEY_MINE: return "Mine";
	case KEY_FASTER: return "Faster";
	case KEY_SLOWER: return "Slower";
	}
	return "None";
}

static void SEC_FRONT KeyLabel(FormType *frm, UInt16 i)
{
	StrPrintF(gKeyLabel[i], "%d: %s", i + 1, KeyName(gKeys[i]));
	CtlSetLabel((ControlType *)Obj(frm, (UInt16)(SetKey1 + i)), gKeyLabel[i]);
}

static Boolean SEC_FRONT SettingsHandler(EventType *e)
{
	FormType *frm = FrmGetActiveForm();
	AppPrefs *p = AppPrefsGet();

	switch (e->eType) {
	case frmOpenEvent: {
		FieldType *fld = (FieldType *)Obj(frm, SetName);
		MemHandle h = MemHandleNew(kNameLen);
		UInt16 i;

		// Closed, so the keyboard doesn't pop up on its own.
		FitForm(frm, false);
		TallLayout(frm, SetOK, SetCancel, 0xFFFF, true);
		ViewInputTrigger();
		if (h) {
			StrCopy((Char *)MemHandleLock(h), p->name);
			MemHandleUnlock(h);
			FldSetTextHandle(fld, h);
		}
		CtlSetValue((ControlType *)Obj(frm, SetSound), p->sound);
		CtlSetValue((ControlType *)Obj(frm, SetSlow), p->autoSlow);
		CtlSetValue((ControlType *)Obj(frm, SetSight), p->autoSight);
		for (i = 0; i < 4; i++) {
			gKeys[i] = p->keys[i];
			KeyLabel(frm, i);
		}
		FrmDrawForm(frm);
		GsiSetLocation(75, (Coord)(146 + gBotShift));
		FrmSetFocus(frm, noFocus);
		return true;
	}
	case frmCloseEvent: {
		FieldType *fld = (FieldType *)Obj(frm, SetName);
		MemHandle h = FldGetTextHandle(fld);

		FldSetTextHandle(fld, NULL);
		if (h)
			MemHandleFree(h);
		return false;
	}
	case ctlSelectEvent: {
		UInt16 id = e->data.ctlSelect.controlID;

		if (id >= SetKey1 && id <= SetKey4) {
			UInt16 i = (UInt16)(id - SetKey1);

			gKeys[i] = (UInt8)((gKeys[i] + 1) % KEY_KINDS);
			KeyLabel(frm, i);
			return true;
		}
		if (id == SetOK) {
			FieldType *fld = (FieldType *)Obj(frm, SetName);
			const Char *t = FldGetTextPtr(fld);
			UInt16 i;

			if (t && t[0])
				StrNCopy(p->name, t, kNameLen - 1);
			else
				StrCopy(p->name, "Player");
			p->name[kNameLen - 1] = 0;
			AppSetSound(CtlGetValue((ControlType *)Obj(frm, SetSound)) != 0);
			p->autoSlow = (UInt8)CtlGetValue((ControlType *)Obj(frm, SetSlow));
			p->autoSight = (UInt8)CtlGetValue((ControlType *)Obj(frm, SetSight));
			for (i = 0; i < 4; i++)
				p->keys[i] = gKeys[i];
			AppPrefsSave();
			Go(TitleForm);
			return true;
		}
		if (id == SetCancel) {
			Go(TitleForm);
			return true;
		}
		return false;
	}
	case winDisplayChangedEvent:
		FitWindow(frm);
		TallLayout(frm, SetOK, SetCancel, 0xFFFF, false);
		FrmDrawForm(frm);
		GsiSetLocation(75, (Coord)(146 + gBotShift));
		return true;
	default:
		break;
	}
	return false;
}

// ---- results ----

static const GameState *gResults;

static void SEC_FRONT Cell(const Char *s, Coord right, Coord y)
{
	WinDrawChars(s, StrLen(s), (Coord)(right - FntCharsWidth(s, StrLen(s))), y);
}

static void SEC_FRONT ResultsDraw(void)
{
	const GameState *g = gResults;
	const AppPrefs *p = AppPrefsGet();
	Char buf[20];
	UInt16 i, j;
	Coord y = (Coord)(20 + gMidShift);

	if (!g)
		return;
	WinDrawChars("Player", 6, 4, y);
	Cell("Kills", 78, y);
	Cell("Died", 104, y);
	Cell("Base", 130, y);
	Cell("Pill", 156, y);
	y = (Coord)(y + 14);
	for (i = 0; i < g->numTanks; i++) {
		const Tank *k = &g->tanks[i];
		UInt16 bases = 0, pills = 0;

		if (i == 0)
			StrCopy(buf, p->name);
		else
			StrPrintF(buf, "Bot %d", i);
		{
			UInt16 n = StrLen(buf);
			Int16 wid = 46;
			Boolean cut = false;

			FntCharsInWidth(buf, &wid, &n, &cut);
			WinDrawChars(buf, n, 4, y);
		}
		for (j = 0; j < g->map.numBases; j++)
			bases += g->map.bases[j].active && g->map.bases[j].owner == k->player;
		for (j = 0; j < g->map.numPills; j++)
			pills += g->map.pills[j].active && g->map.pills[j].owner == k->player;
		StrPrintF(buf, "%d", k->kills);
		Cell(buf, 78, y);
		StrPrintF(buf, "%d", k->deaths);
		Cell(buf, 104, y);
		StrPrintF(buf, "%d", bases);
		Cell(buf, 130, y);
		StrPrintF(buf, "%d", pills);
		Cell(buf, 156, y);
		y = (Coord)(y + 12);
	}
}

static Boolean SEC_FRONT ResultsHandler(EventType *e)
{
	FormType *frm = FrmGetActiveForm();

	switch (e->eType) {
	case frmOpenEvent:
		FitForm(frm, false);
		TallLayout(frm, ResultsOK, 0xFFFF, 0xFFFF, true);
		FrmDrawForm(frm);
		ResultsDraw();
		return true;
	case ctlSelectEvent:
		if (e->data.ctlSelect.controlID == ResultsOK) {
			gResults = NULL;
			Go(TitleForm);
			return true;
		}
		return false;
	case winDisplayChangedEvent:
		FitWindow(frm);
		TallLayout(frm, ResultsOK, 0xFFFF, 0xFFFF, false);
		FrmDrawForm(frm);
		ResultsDraw();
		return true;
	default:
		break;
	}
	return false;
}

void SEC_FRONT FrontResults(const GameState *g)
{
	gResults = g;
	FrmGotoForm(ResultsForm);
}

FormEventHandlerType *SEC_FRONT FrontHandler(UInt16 formId)
{
	switch (formId) {
	case TitleForm: return TitleHandler;
	case MapListForm: return MapListHandler;
	case NewGameForm: return NewGameHandler;
	case SettingsForm: return SettingsHandler;
	case ResultsForm: return ResultsHandler;
	}
	return NULL;
}

#ifdef DEBUG
// Plays ticks of a game on the loaded map with nobody at the keys.
static void SEC_FRONT PlayTicks(GameState *g, UInt32 n)
{
	GameInput none[GAME_MAX_TANKS];

	MemSet(none, sizeof(none), 0);
	while (n--)
		game_tick(g, none);
}

// A game of 600 ticks run straight through, and the same game saved after
// 300, another game started in between and the save read back: the
// checksums must match. This replaces any saved game.
void SEC_FRONT FrontSaveTest(GameState *g)
{
	Char name[] = "Everard Island", hex[9], hex2[9];
	GameSetup setup;
	UInt32 a, b, ta, tb;

	MemSet(&setup, sizeof(setup), 0);
	setup.numPlayers = 1;
	setup.numBots = 3;
	setup.botLevel = BOT_NORMAL;
	if (MapLoad(0, name, g) != MAP_OK)
		return;
	game_init(g, 777, &setup);
	PlayTicks(g, 600);
	a = game_checksum(g);
	ta = game_terrain_checksum();
	if (MapLoad(0, name, g) != MAP_OK)
		return;
	game_init(g, 777, &setup);
	PlayTicks(g, 300);
	if (!SaveWrite(g)) {
		LOG(("save test: can't save"));
		return;
	}
	if (MapLoad(0, name, g) != MAP_OK)
		return;
	game_init(g, 4242, &setup);
	PlayTicks(g, 100);
	if (!SaveRead(g)) {
		LOG(("save test: can't read"));
		return;
	}
	PlayTicks(g, 300);
	b = game_checksum(g);
	tb = game_terrain_checksum();
	HexStr(a, hex);
	HexStr(b, hex2);
	LOG(("save and resume %s %s terrain %s %s", hex, hex2, ta == tb ? "same" : "DIFFERENT",
		a == b && ta == tb ? "ok" : "WRONG"));
	SaveDrop();
}
#endif

#if defined(DEBUG) && defined(DEMO_SHOWCASE)
// For the art replay: the tank on land six squares west of the first base
// with the first two pills on board, its builder under a parachute three
// squares north flying to four squares east of it, and a wreck flying east
// from three squares south.
void SEC_FRONT DemoShowcase(struct GameState *g)
{
	Tank *k = &g->tanks[game_viewer_tank(g)];
	Builder *m = &g->men[game_viewer_tank(g)];
	TankExplosion *e;
	UInt8 i;

	if (g->map.numBases > 0) {
		k->x = (UInt16)(((UInt16)g->map.bases[0].x - 6) * 256 + 128);
		k->y = (UInt16)((UInt16)g->map.bases[0].y * 256 + 128);
	}
	k->angle = 64 * ANGLE_ONE;
	k->boat = BOAT_OFF;
	for (i = 0; i < 2 && i < g->map.numPills; i++) {
		g->map.pills[i].inTank = 1;
		g->map.pills[i].owner = k->player;
		k->carry[k->numCarry++] = i;
	}
	m->inTank = 0;
	m->isDead = 1;
	m->frame = LGM_HELICOPTER_FRAME;
	m->x = k->x;
	m->y = (UInt16)(k->y - 3 * 256);
	m->destX = (UInt16)(k->x + 4 * 256);
	m->destY = k->y;
	e = &g->tkExpl[0];
	e->x = k->x;
	e->y = (UInt16)(k->y + 3 * 256);
	e->angle = 64 * ANGLE_ONE;
	e->length = 255;
	e->big = 0;
	e->creator = GAME_NO_PLAYER;
	g->numTkExpl = 1;
}
#endif
