// The screens around the game: title, new game (map list and settings),
// the player's settings and the results, and the saved game. All in a code
// segment of their own, and the title picture is loaded only while the
// title is on screen.
#define ALLOW_ACCESS_TO_INTERNALS_OF_CONTROLS
#include "palmbolo.h"
#include "mapio.h"
#include "resource.h"
#include "frontlayout.h"
#include "art.h"
#include "game/game.h"
#include "game/mapstore.h"
#include "game/util.h"
#include "game/mapthumb.h"
#include "netgame.h"
#include "net/udp.h"

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

enum { UI_INK, UI_PAPER, UI_DARK, UI_LIGHT, UI_GOLD, UI_OLIVE, UI_EDGE, UI_COUNT };
static const UInt8 gUiRGB[UI_COUNT][3] = {
	{37,52,38}, {229,227,201}, {51,75,54}, {246,242,217},
	{237,200,94}, {96,114,73}, {139,150,112}
};
static const UInt8 gUiGrey[UI_COUNT] = {0,255,0,255,170,85,85};
static IndexedColorType gUi[UI_COUNT];
static Boolean gUiPushed;
static Boolean gTitleHaveSave;

static void SEC_FRONT UiColour(UIColorTableEntries entry, UInt16 colour)
{
	RGBColorType rgb;

	if (!gUiPushed) return;
	rgb.index = 0;
	if (ScreenDepth() >= 8) {
		rgb.r = gUiRGB[colour][0]; rgb.g = gUiRGB[colour][1]; rgb.b = gUiRGB[colour][2];
	} else rgb.r = rgb.g = rgb.b = gUiGrey[colour];
	UIColorSetTableEntry(entry, &rgb);
}

static void SEC_FRONT UiBegin(void)
{
	UInt16 i;
	Boolean colour = ScreenDepth() >= 8;

	for (i = 0; i < UI_COUNT; i++)
		gUi[i] = colour ? ScreenIndex(gUiRGB[i][0], gUiRGB[i][1], gUiRGB[i][2]) :
			ScreenIndex(gUiGrey[i], gUiGrey[i], gUiGrey[i]);
	gUiPushed = false;
	if (!ScreenNewWin()) return;
	WinPushDrawState();
	gUiPushed = UIColorPushTable() == errNone;
	UiColour(UIFormFill, UI_PAPER);
	UiColour(UIObjectFill, UI_PAPER);
	UiColour(UIObjectFrame, UI_INK);
	UiColour(UIObjectForeground, UI_INK);
	UiColour(UIObjectSelectedFill, UI_DARK);
	UiColour(UIObjectSelectedForeground, UI_LIGHT);
	UiColour(UIFieldBackground, UI_PAPER);
	UiColour(UIFieldText, UI_INK);
	UiColour(UIFieldTextLines, UI_EDGE);
	UiColour(UIFieldCaret, UI_INK);
	WinSetForeColor(gUi[UI_INK]);
	WinSetTextColor(gUi[UI_INK]);
	WinSetBackColor(gUi[UI_PAPER]);
}

static void SEC_FRONT UiEnd(void)
{
	if (gUiPushed) UIColorPopTable();
	gUiPushed = false;
	if (ScreenNewWin()) WinPopDrawState();
}

static void SEC_FRONT Fill(Coord x, Coord y, Coord w, Coord h, UInt16 colour)
{
	RectangleType r;
	if (w <= 0 || h <= 0) return;
	RctSetRectangle(&r, x, y, w, h);
	if (ScreenNewWin()) {
		WinSetForeColor(gUi[colour]);
		WinDrawRectangle(&r, 0);
	} else if (gUi[colour]) WinDrawRectangle(&r, 0);
	else WinEraseRectangle(&r, 0);
}

static void SEC_FRONT Text(const Char *s, Coord x, Coord y, Coord width,
	UInt16 fg, UInt16 bg)
{
	UInt16 len = StrLen(s);
	Int16 w = width;
	Boolean fit;
	FntCharsInWidth(s, &w, &len, &fit);
	if (ScreenNewWin()) {
		WinSetTextColor(gUi[fg]);
		WinSetBackColor(gUi[bg]);
		WinDrawChars(s, len, x, y);
	} else if (!gUi[fg]) WinDrawInvertedChars(s, len, x, y);
	else WinDrawChars(s, len, x, y);
}

static void SEC_FRONT Section(const Char *s, Coord y)
{
	Fill(6, y, 148, 12, UI_OLIVE);
	Text(s, 10, y + 1, 140, UI_LIGHT, UI_OLIVE);
}

static void SEC_FRONT TankMark(Coord x, Coord y)
{
	Fill(x, y + 2, 2, 8, UI_INK);
	Fill(x + 8, y + 2, 2, 8, UI_INK);
	Fill(x + 2, y + 3, 6, 6, UI_OLIVE);
	Fill(x + 4, y, 2, 6, UI_INK);
}

static UInt16 SEC_FRONT Primary(FormType *frm)
{
	switch (FrmGetFormId(frm)) {
	case TitleForm: return gTitleHaveSave ? TitleResume : TitleNew;
	case MapListForm: return MapNext;
	case NewGameForm: return NgStart;
	case LobbyForm: return LobStart;
	case JoinForm: return JoinGo;
	case SettingsForm: return SetOK;
	case ResultsForm: return ResultsOK;
	}
	return 0;
}

// The OS still draws and tracks each button, including its focus ring.
static void SEC_FRONT DrawButtons(FormType *frm)
{
	UInt16 i, n = FrmGetNumberOfObjects(frm), primary = Primary(frm);
	for (i = 0; i < n; i++) if (FrmGetObjectType(frm, i) == frmControlObj) {
		ControlType *ctl = FrmGetObjectPtr(frm, i);
		if (ctl->style == buttonCtl && ctl->attr.usable) {
			UiColour(UIObjectFill, ctl->id == primary ? UI_GOLD : UI_PAPER);
			CtlDrawControl(ctl);
		}
	}
	UiColour(UIObjectFill, UI_PAPER);
}

static void SEC_FRONT DrawFields(FormType *frm)
{
	UInt16 i, n = FrmGetNumberOfObjects(frm);
	for (i = 0; i < n; i++) {
		FormObjectKind kind = FrmGetObjectType(frm, i);
		RectangleType r;
		if (kind == frmControlObj) {
			ControlType *ctl = FrmGetObjectPtr(frm, i);
			if (!ctl->attr.usable || ctl->style != popupTriggerCtl) continue;
		} else if (kind != frmFieldObj) continue;
		FrmGetObjectBounds(frm, i, &r);
		Fill(r.topLeft.x - 2, r.topLeft.y - 1, r.extent.x + 3, 1, UI_EDGE);
		Fill(r.topLeft.x - 2, r.topLeft.y + r.extent.y, r.extent.x + 3, 1, UI_EDGE);
		Fill(r.topLeft.x - 2, r.topLeft.y, 1, r.extent.y, UI_EDGE);
		Fill(r.topLeft.x + r.extent.x, r.topLeft.y, 1, r.extent.y, UI_EDGE);
	}
}

// ---- tall screens ----

static Coord gMidShift, gBotShift;
static Coord gBaseY[64];

// Keep compact forms top-aligned; use extra height for groups and lists.
static void SEC_FRONT TallLayout(FormType *frm, UInt16 bot1, UInt16 bot2, UInt16 bot3,
	UInt16 list, Boolean reset)
{
	RectangleType r;
	Coord extra, x, y, shift;
	UInt16 i, n = FrmGetNumberOfObjects(frm), form = FrmGetFormId(frm);

	ScreenDisplayRect(&r);
	extra = r.extent.y > 160 ? r.extent.y - 160 : 0;
	gBotShift = extra;
	gMidShift = extra / 4;
	for (i = 0; i < n && i < 64; i++) {
		UInt16 id = FrmGetObjectId(frm, i);
		FormObjectKind kind = FrmGetObjectType(frm, i);
		if (kind == frmTitleObj || kind == frmGraffitiStateObj || kind == frmPopupObj)
			continue;
		FrmGetObjectPosition(frm, i, &x, &y);
		if (reset) gBaseY[i] = y;
		y = gBaseY[i];
		shift = 0;
		if (id == bot1 || id == bot2 || id == bot3) shift = extra;
		else shift = front_shift(form, y, r.extent.y);
		if (id == list) {
			y = front_map_list_y(r.extent.y);
			LstSetPosition(FrmGetObjectPtr(frm, i), x, y);
			LstSetHeight(FrmGetObjectPtr(frm, i), (r.extent.y - 28 - y) / 11);
		} else if (kind == frmListObj) {
			LstSetPosition(FrmGetObjectPtr(frm, i), x, y + shift);
		} else FrmSetObjectPosition(frm, i, x, y + shift);
	}
}

static void SEC_FRONT FormDraw(FormType *frm)
{
	RectangleType r;
	UInt16 form = FrmGetFormId(frm);
	FontID old = FntSetFont(stdFont);
	Coord extra;

	FrmDrawForm(frm);
	ScreenDisplayRect(&r);
	extra = r.extent.y - 160;
	if (form == TitleForm) {
		Fill(0, 111, 160, r.extent.y - 111, UI_DARK);
		Fill(0, 110, 160, 2, UI_GOLD);
	} else {
		Fill(0, 0, 160, 20, UI_DARK);
		Fill(0, 20, 160, 2, UI_GOLD);
		Text(FrmGetTitle(frm), 7, 5, 146, UI_LIGHT, UI_DARK);
		Fill(0, r.extent.y - 22, 160, 22, UI_DARK);
		Fill(0, r.extent.y - 23, 160, 1, UI_EDGE);
		if (form == NewGameForm && extra >= 50) {
			Text(AppPrefsGet()->map, 7, 27, 146, UI_INK, UI_PAPER);
			Section("Game rules", 27 + extra / 2 - 15);
			Section("Players", 78 + extra / 2 + 10 - 15);
		} else if (form == SettingsForm) {
			Section("Buttons", 86 + gMidShift);
			Fill(72, r.extent.y - 16, 16, 13, UI_PAPER);
		}
	}
	DrawFields(frm);
	DrawButtons(frm);
	if (ScreenNewWin()) {
		WinSetTextColor(gUi[UI_INK]);
		WinSetBackColor(gUi[UI_PAPER]);
		WinSetForeColor(gUi[UI_INK]);
	}
	FntSetFont(old);
}

// Network settings last for this app session without changing saved preferences.
static Char gUdpHost[16];
static Char gUdpPort[6] = "5046";

static Boolean SEC_FRONT NetworkDialog(Boolean host)
{
	FormType *frm = FrmInitForm(NetSetupForm), *old = FrmGetActiveForm();
	MemHandle addr = NULL, port = NULL;
	UInt16 result, number;
	UInt32 ip = 0;
	Boolean ok = false;
	if (!frm) return false;
	addr = MemHandleNew(sizeof(gUdpHost)); port = MemHandleNew(sizeof(gUdpPort));
	if (!addr || !port) {
		if (addr) MemHandleFree(addr);
		if (port) MemHandleFree(port);
		addr = port = NULL;
		goto done;
	}
	StrCopy(MemHandleLock(addr), gUdpHost); MemHandleUnlock(addr);
	StrCopy(MemHandleLock(port), gUdpPort); MemHandleUnlock(port);
	FldSetTextHandle(Obj(frm, NetSetupAddress), addr);
	FldSetTextHandle(Obj(frm, NetSetupPort), port);
	if (host) {
		FrmHideObject(frm, FrmGetObjectIndex(frm, NetSetupAddress));
		FrmHideObject(frm, FrmGetObjectIndex(frm, NetSetupAddressLabel));
	}
	ViewInputTrigger();
	for (;;) {
		const Char *a, *p;
		result = FrmDoDialog(frm);
		if (result != NetSetupOK) break;
		a = FldGetTextPtr(Obj(frm, NetSetupAddress));
		p = FldGetTextPtr(Obj(frm, NetSetupPort));
		if (!udp_port(p ? p : "", &number) || (!host && !udp_ip(a ? a : "", &ip))) {
			FrmCustomAlert(ViewAlert, "Enter an IPv4 address and port 1-65535.", "", "");
			continue;
		}
		if (!host) StrCopy(gUdpHost, a);
		StrCopy(gUdpPort, p);
		udp_config(ip, number); ok = true; break;
	}
	done:
	// Fields can resize their handles while editing.
	if (addr) { addr = FldGetTextHandle(Obj(frm, NetSetupAddress)); FldSetTextHandle(Obj(frm, NetSetupAddress), NULL); if (addr) MemHandleFree(addr); }
	if (port) { port = FldGetTextHandle(Obj(frm, NetSetupPort)); FldSetTextHandle(Obj(frm, NetSetupPort), NULL); if (port) MemHandleFree(port); }
	FrmDeleteForm(frm);
	FrmSetActiveForm(old);
	return ok;
}

// ---- links ----

static UInt8 gLinkKinds[kNetMaxLinks];
static const Char *gLinkNames[kNetMaxLinks];
static UInt8 gNumLinks, gLinkSel;

static UInt8 SEC_FRONT FindLinks(void)
{
	gNumLinks = NetLinks(gLinkKinds, gLinkNames);
	if (gLinkSel >= gNumLinks)
		gLinkSel = 0;
	return gNumLinks;
}

// ---- title ----

static MemHandle gTitleRes;     // the picture, locked while the title shows

static void SEC_FRONT TitleLayout(FormType *frm, Boolean haveSave)
{
	UInt16 ids[4];
	RectangleType r;
	Coord h;
	UInt16 i, n = 0;

	gTitleHaveSave = haveSave;
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
		if (h < 210) {
			x = (Coord)(n % 2 ? 82 : 4);
			y = (Coord)(113 + (n / 2) * 17);
		} else {
			x = 27;
			y = (Coord)(116 + (h - 210) / 2 + n * 21);
		}
		{
			RectangleType bounds;
			FrmGetObjectBounds(frm, idx, &bounds);
			bounds.topLeft.x = x; bounds.topLeft.y = y;
			bounds.extent.x = h < 210 ? 74 : 106;
			FrmSetObjectBounds(frm, idx, &bounds);
		}
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

	FormDraw(frm);
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
	Text(ver, (Coord)(158 - FntCharsWidth(ver, StrLen(ver))),
		(Coord)(r.extent.y - FntLineHeight() - 1), 158, UI_LIGHT, UI_DARK);
	FntSetFont(old);
}

static Boolean SEC_FRONT TitleHandler(EventType *e)
{
	FormType *frm = FrmGetActiveForm();

	switch (e->eType) {
	case frmOpenEvent:
		FitForm(frm, false);
		TitleLayout(frm, SaveExists());
		CtlSetEnabled(Obj(frm, TitleJoin), FindLinks() > 0);
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
		if (!gNumLinks && RctPtInRectangle(e->screenX, e->screenY, &b)) {
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
			Go(MapListForm);
			return true;
		case TitleSettings:
			Go(SettingsForm);
			return true;
		case TitleJoin:
			Go(JoinForm);
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
static UInt16 gMapPage;
static UInt16 gMapIndex;        // chosen in the list
static UInt8 *gThumb;
static Boolean gThumbValid;
static const UInt8 gThumbRGB[THUMB_COLOURS][3] = {
	{80,112,120}, {117,149,149}, {166,177,110}, {68,104,61},
	{239,232,187}, {91,84,71}, {237,200,94}, {30,35,26}
};
static const UInt8 gThumbGrey[THUMB_COLOURS] = {170,170,255,85,255,0,0,0};
static const CustomPatternType gSeaPattern = {0x11,0,0,0,0x11,0,0,0};

static void SEC_FRONT MapThumbDraw(void)
{
	RectangleType r;
	Coord size, left, x, y, end, sx, sy, ex, ey;
	IndexedColorType colours[THUMB_COLOURS];
	UInt16 i, value;
	CustomPatternType oldPattern;
	Boolean stipple = !ScreenNewWin() || ScreenDepth() == 1;
	ScreenDisplayRect(&r);
	size = front_map_size(r.extent.y);
	left = (160 - size) / 2;
	Fill(left - 1, 25, size + 2, size + 2, UI_INK);
	if (!gThumbValid) {
		Fill(left, 26, size, size, UI_PAPER);
		Text("No preview", left + 3, 26 + size / 2 - 5, size - 6, UI_INK, UI_PAPER);
		return;
	}
	for (i = 0; i < THUMB_COLOURS; i++)
		colours[i] = ScreenDepth() >= 8 ? ScreenIndex(gThumbRGB[i][0], gThumbRGB[i][1], gThumbRGB[i][2]) :
			ScreenIndex(gThumbGrey[i], gThumbGrey[i], gThumbGrey[i]);
	if (stipple) {
		WinGetPattern(&oldPattern);
		WinSetPattern(&gSeaPattern);
	}
	for (y = 0; y < MAP_THUMB_SIZE; y++) {
		sy = 26 + y * size / MAP_THUMB_SIZE;
		ey = 26 + (y + 1) * size / MAP_THUMB_SIZE;
		for (x = 0; x < MAP_THUMB_SIZE; x = end) {
			value = gThumb[y * MAP_THUMB_SIZE + x];
			for (end = x + 1; end < MAP_THUMB_SIZE && gThumb[y * MAP_THUMB_SIZE + end] == value; end++);
			sx = left + x * size / MAP_THUMB_SIZE;
			ex = left + end * size / MAP_THUMB_SIZE;
			RctSetRectangle(&r, sx, sy, ex - sx, ey - sy);
			if (stipple && value == THUMB_SEA) {
				if (ScreenNewWin()) {
					WinSetForeColor(gUi[UI_INK]);
					WinSetBackColor(gUi[UI_PAPER]);
				}
				WinFillRectangle(&r, 0);
			} else if (ScreenNewWin()) {
				WinSetForeColor(colours[value]);
				WinDrawRectangle(&r, 0);
			} else if (colours[value]) WinDrawRectangle(&r, 0);
			else WinEraseRectangle(&r, 0);
		}
	}
	if (stipple) WinSetPattern((const CustomPatternType *)&oldPattern);
}

static void SEC_FRONT MapThumbLoad(Int16 sel)
{
	gThumbValid = gThumb && sel >= 0 && sel < gNumMaps && MapPreview(sel, gNames[sel], gThumb);
}

static Boolean SEC_FRONT MapListHandler(EventType *e)
{
	FormType *frm = FrmGetActiveForm();
	ListType *lst;
	Char selected[kMapNameLen];
	StrCopy(selected, AppPrefsGet()->map);
	if (e->eType == frmUpdateEvent) {
		Int16 sel = LstGetSelection((ListType *)Obj(frm, MapList));
		if (sel >= 0 && sel < gNumMaps) StrCopy(selected, gNames[sel]);
	}

	switch (e->eType) {
	case menuEvent:
		if (e->data.menu.itemID == MapPageItem) {
			gMapPage = gNumMaps == kMaxMaps ? gMapPage + kMaxMaps - 1 : 0;
			FrmUpdateForm(MapListForm, frmRedrawUpdateCode);
			return true;
		}
		if (e->data.menu.itemID == MapImportItem || e->data.menu.itemID == MapBeamItem) {
			Int16 sel = LstGetSelection((ListType *)Obj(frm, MapList));
			if (e->data.menu.itemID == MapImportItem) MapBrowse();
			else if (sel >= 0 && sel < gNumMaps) MapBeam(sel, gNames[sel]);
			FrmUpdateForm(MapListForm, frmRedrawUpdateCode);
			return true;
		}
		return false;
	case frmUpdateEvent:
		if (gThumb) MemPtrFree(gThumb);
		gThumb = NULL;
		// Rebuild after an import or a receive while the list was open.
	case frmOpenEvent: {
		UInt16 i, sel = 0;

		FitForm(frm, false);
		lst = (ListType *)Obj(frm, MapList);
		gNumMaps = MapListBuild(gNames, gMapPage);
		for (i = 0; i < gNumMaps; i++) {
			gNamePtrs[i] = gNames[i];
			if (StrCompare(gNames[i], selected) == 0)
				sel = i;
		}
		LstSetListChoices(lst, gNamePtrs, gNumMaps);
		TallLayout(frm, MapNext, MapBack, 0xFFFF, MapList, e->eType == frmOpenEvent);
		LstSetSelection(lst, sel);
		gThumb = MemPtrNew(MAP_THUMB_SIZE * MAP_THUMB_SIZE);
		MapThumbLoad(sel);
		FormDraw(frm);
		MapThumbDraw();
		return true;
	}
	case lstSelectEvent:
		if (e->data.lstSelect.listID == MapList) {
			MapThumbLoad(e->data.lstSelect.selection);
			MapThumbDraw();
		}
		return false;
	case frmCloseEvent:
		if (gThumb) MemPtrFree(gThumb);
		gThumb = NULL;
		gThumbValid = false;
		return false;
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
		TallLayout(frm, MapNext, MapBack, 0xFFFF, MapList, false);
		FormDraw(frm);
		MapThumbDraw();
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
		TallLayout(frm, NgStart, NgBack, NgHost, 0xFFFF, true);
		if (!FindLinks())
			FrmHideObject(frm, FrmGetObjectIndex(frm, NgHost));
		SetPopup(frm, NgTypeTrig, NgTypeList, p->gameType);
		CtlSetValue((ControlType *)Obj(frm, NgMines), p->hiddenMines);
		SetPopup(frm, NgTimeTrig, NgTimeList, p->timeLimit);
		SetPopup(frm, NgBotsTrig, NgBotsList, p->numBots);
		SetPopup(frm, NgLevelTrig, NgLevelList, p->botLevel);
		SetPopup(frm, NgTeamsTrig, NgTeamsList, p->teams);
		FormDraw(frm);
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
			// Network games aren't saved, so only this asks.
			if (SaveExists() && FrmAlert(ReplaceAlert) != 0)
				return true;
			AppOpenMap(gMapIndex, gNames[gMapIndex]);
			return true;
		}
		if (e->data.ctlSelect.controlID == NgBack) {
			p->hiddenMines = (UInt8)CtlGetValue((ControlType *)Obj(frm, NgMines));
			AppPrefsSave();
			Go(MapListForm);
			return true;
		}
		if (e->data.ctlSelect.controlID == NgHost) {
			p->hiddenMines = (UInt8)CtlGetValue((ControlType *)Obj(frm, NgMines));
			AppPrefsSave();
			if (!FindLinks())
				return true;
			if (gLinkKinds[gLinkSel] == LINK_UDP && !NetworkDialog(true)) return true;
			if (!NetHost(gLinkKinds[gLinkSel], gMapIndex, gNames[gMapIndex], AppGame())) {
				FrmCustomAlert(ViewAlert, NetWhy(), "", "");
				return true;
			}
			Go(LobbyForm);
			return true;
		}
		return false;
	case winDisplayChangedEvent:
		FitWindow(frm);
		TallLayout(frm, NgStart, NgBack, NgHost, 0xFFFF, false);
		FormDraw(frm);
		return true;
	default:
		break;
	}
	return false;
}

// ---- lobby: hosting and joining ----

static UInt16 gLobbySeq;

static void SEC_FRONT AddressButton(FormType *frm)
{
	UInt16 i = FrmGetObjectIndex(frm, LobAddress);
	if (gNumLinks && gLinkKinds[gLinkSel] == LINK_UDP) FrmShowObject(frm, i);
	else FrmHideObject(frm, i);
}

// The map, a row for each tank and the status, between the link and the
// buttons.
static void SEC_FRONT LobbyDraw(void)
{
	RectangleType r;
	Char name[kNameLen], note[20], line[48];
	UInt8 i, n;
	Coord y, rowHeight, status;
	FontID old = FntSetFont(stdFont);

	ScreenDisplayRect(&r);
	gLobbySeq = NetLobbySeq();
	status = r.extent.y - 34;
	Fill(0, 38, 160, r.extent.y - 61, UI_PAPER);
	if (r.extent.y >= 198) {
		Text(NetMapName(), 7, 42, 146, UI_INK, UI_PAPER);
		Section("Players", 57);
		y = front_lobby_y(r.extent.y);
		rowHeight = front_lobby_row(r.extent.y);
	} else {
		y = front_lobby_y(r.extent.y);
		rowHeight = front_lobby_row(r.extent.y);
	}
	n = NetRows();
	for (i = 0; i < n && i < GAME_MAX_TANKS; i++) {
		UInt16 bg = i % 2 ? UI_PAPER : UI_LIGHT;
		NetRow(i, name, note);
		Fill(6, y, 148, rowHeight, bg);
		TankMark(9, y);
		Text(name, 24, y, 80, UI_INK, bg);
		Text(note, 106, y, 47, UI_INK, bg);
		y += rowHeight;
	}
	NetStatus(line);
	Fill(6, status, 148, 12, UI_LIGHT);
	Text(line, 9, status, 142, UI_INK, UI_LIGHT);
	FntSetFont(old);
}

static void SEC_FRONT LinkPopup(FormType *frm, UInt16 trig, UInt16 list)
{
	ListType *lst = (ListType *)Obj(frm, list);

	FindLinks();
	LstSetListChoices(lst, (Char **)gLinkNames, gNumLinks);
	LstSetHeight(lst, gNumLinks);
	if (gNumLinks)
		SetPopup(frm, trig, list, gLinkSel);
}

// Polls the network while a lobby form shows. Goes to the game when it
// starts, or says why it stopped and goes to the title.
static void SEC_FRONT LobbyIdle(void)
{
	UInt8 phase = NetPoll();

	if (phase == NET_PLAY) {
		AppNetGame();
		return;
	}
	if (phase == NET_STOPPED) {
		Char why[48];

		StrCopy(why, NetWhy());
		NetEnd();
		if (why[0])
			FrmCustomAlert(ViewAlert, why, "", "");
		Go(TitleForm);
		return;
	}
	if (NetLobbySeq() != gLobbySeq)
		LobbyDraw();
}

static void SEC_FRONT ShowObj(FormType *frm, UInt16 id, Boolean show)
{
	UInt16 i = FrmGetObjectIndex(frm, id);

	if (show) {
		UiColour(UIObjectFill, id == Primary(frm) ? UI_GOLD : UI_PAPER);
		FrmShowObject(frm, i);
		UiColour(UIObjectFill, UI_PAPER);
	} else {
		RectangleType r;
		FrmGetObjectBounds(frm, i, &r);
		FrmHideObject(frm, i);
		Fill(r.topLeft.x - 2, r.topLeft.y - 2, r.extent.x + 4, r.extent.y + 4, UI_DARK);
	}
}

static Boolean SEC_FRONT LobbyHandler(EventType *e)
{
	FormType *frm = FrmGetActiveForm();

	switch (e->eType) {
	case frmOpenEvent:
		FitForm(frm, false);
		TallLayout(frm, LobStart, LobCancel, 0xFFFF, 0xFFFF, true);
		LinkPopup(frm, LobLinkTrig, LobLinkList);
		AddressButton(frm);
		FormDraw(frm);
		LobbyDraw();
		return true;
	case nilEvent:
		LobbyIdle();
		return true;
	case popSelectEvent:
		if (e->data.popSelect.selection < 0 || e->data.popSelect.selection >= gNumLinks) return true;
		if (e->data.popSelect.listID == LobLinkList &&
		    e->data.popSelect.selection != gLinkSel && NetPhase() == NET_LOBBY) {
			if (gLinkKinds[e->data.popSelect.selection] == LINK_UDP && !NetworkDialog(true)) {
				SetPopup(frm, LobLinkTrig, LobLinkList, gLinkSel);
				return true;
			}
			gLinkSel = (UInt8)e->data.popSelect.selection;
			if (!NetHost(gLinkKinds[gLinkSel], gMapIndex, gNames[gMapIndex], AppGame())) {
				FrmCustomAlert(ViewAlert, NetWhy(), "", "");
				Go(NewGameForm);
				return true;
			}
			AddressButton(frm);
			FormDraw(frm);
			LobbyDraw();
		}
		return false;
	case ctlSelectEvent:
		if (e->data.ctlSelect.controlID == LobAddress) {
			Char address[48];
			udp_local(address);
			FrmCustomAlert(ViewAlert, address, "", "");
			return true;
		}
		if (e->data.ctlSelect.controlID == LobStart) {
			if (!NetCanStart())
				return true;
			if (!NetStart()) {
				FrmCustomAlert(ViewAlert, NetWhy(), "", "");
				return true;
			}
			ShowObj(frm, LobStart, false);
			LobbyDraw();
			return true;
		}
		if (e->data.ctlSelect.controlID == LobCancel) {
			NetEnd();
			Go(NewGameForm);
			return true;
		}
		return false;
	case winDisplayChangedEvent:
		FitWindow(frm);
		TallLayout(frm, LobStart, LobCancel, 0xFFFF, 0xFFFF, false);
		FormDraw(frm);
		LobbyDraw();
		return true;
	default:
		break;
	}
	return false;
}

static Boolean SEC_FRONT JoinHandler(EventType *e)
{
	FormType *frm = FrmGetActiveForm();

	switch (e->eType) {
	case frmOpenEvent:
		FitForm(frm, false);
		TallLayout(frm, JoinGo, JoinBack, 0xFFFF, 0xFFFF, true);
		LinkPopup(frm, JoinLinkTrig, JoinLinkList);
		FormDraw(frm);
		LobbyDraw();
		return true;
	case nilEvent:
		if (NetPhase() != NET_OFF)
			LobbyIdle();
		return true;
	case popSelectEvent:
		if (e->data.popSelect.selection < 0 || e->data.popSelect.selection >= gNumLinks) return true;
		if (e->data.popSelect.listID == JoinLinkList &&
		    e->data.popSelect.selection != gLinkSel) {
			// Another link: look again from the start.
			gLinkSel = (UInt8)e->data.popSelect.selection;
			NetEnd();
			ShowObj(frm, JoinGo, true);
			LobbyDraw();
		}
		return false;
	case ctlSelectEvent:
		if (e->data.ctlSelect.controlID == JoinGo) {
			if (!gNumLinks)
				return true;
			if (gLinkKinds[gLinkSel] == LINK_UDP && !NetworkDialog(false)) return true;
			if (!NetJoin(gLinkKinds[gLinkSel], AppGame())) {
				FrmCustomAlert(ViewAlert, NetWhy(), "", "");
				return true;
			}
			ShowObj(frm, JoinGo, false);
			LobbyDraw();
			return true;
		}
		if (e->data.ctlSelect.controlID == JoinBack) {
			NetEnd();
			Go(TitleForm);
			return true;
		}
		return false;
	case winDisplayChangedEvent:
		FitWindow(frm);
		TallLayout(frm, JoinGo, JoinBack, 0xFFFF, 0xFFFF, false);
		FormDraw(frm);
		LobbyDraw();
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
		TallLayout(frm, SetOK, SetCancel, 0xFFFF, 0xFFFF, true);
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
		FormDraw(frm);
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
		TallLayout(frm, SetOK, SetCancel, 0xFFFF, 0xFFFF, false);
		FormDraw(frm);
		GsiSetLocation(75, (Coord)(146 + gBotShift));
		return true;
	default:
		break;
	}
	return false;
}

// ---- results ----

static const GameState *gResults;
static Boolean gResultsNet;

static void SEC_FRONT ResultsDraw(void)
{
	const GameState *g = gResults;
	const AppPrefs *p = AppPrefsGet();
	RectangleType r;
	Char buf[20];
	UInt16 i, j, bg;
	Coord y, rowHeight;
	FontID old = FntSetFont(stdFont);

	if (!g) { FntSetFont(old); return; }
	ScreenDisplayRect(&r);
	y = 28;
	if (r.extent.y >= 210) {
		TankMark(9, 30);
		Text("Match results", 26, 30, 126, UI_INK, UI_PAPER);
		y = 50;
	}
	Fill(6, y, 148, 12, UI_OLIVE);
	Text("Player", 9, y, 52, UI_LIGHT, UI_OLIVE);
	Text("Kills", 64, y, 24, UI_LIGHT, UI_OLIVE);
	Text("Died", 88, y, 24, UI_LIGHT, UI_OLIVE);
	Text("Base", 112, y, 24, UI_LIGHT, UI_OLIVE);
	Text("Pill", 136, y, 18, UI_LIGHT, UI_OLIVE);
	y = front_results_y(r.extent.y);
	rowHeight = front_results_row(r.extent.y);
	for (i = 0; i < g->numTanks; i++) {
		const Tank *k = &g->tanks[i];
		UInt16 bases = 0, pills = 0;

		bg = i == (gResultsNet ? NetSlot() : game_viewer_tank(g)) ? UI_GOLD : i % 2 ? UI_PAPER : UI_LIGHT;
		Fill(6, y, 148, rowHeight, bg);
		if (gResultsNet && NetName((UInt8)i)[0]) StrCopy(buf, NetName((UInt8)i));
		else if (i == game_viewer_tank(g) && !gResultsNet) StrCopy(buf, p->name);
		else StrPrintF(buf, "Bot %d", i);
		Text(buf, 9, y, 52, UI_INK, bg);
		for (j = 0; j < g->map.numBases; j++) bases += g->map.bases[j].active && g->map.bases[j].owner == k->player;
		for (j = 0; j < g->map.numPills; j++) pills += g->map.pills[j].active && g->map.pills[j].owner == k->player;
		StrPrintF(buf, "%d", k->kills); Text(buf, 64, y, 24, UI_INK, bg);
		StrPrintF(buf, "%d", k->deaths); Text(buf, 88, y, 24, UI_INK, bg);
		StrPrintF(buf, "%d", bases); Text(buf, 112, y, 24, UI_INK, bg);
		StrPrintF(buf, "%d", pills); Text(buf, 136, y, 18, UI_INK, bg);
		y += rowHeight;
	}
	FntSetFont(old);
}

static Boolean SEC_FRONT ResultsHandler(EventType *e)
{
	FormType *frm = FrmGetActiveForm();

	switch (e->eType) {
	case frmOpenEvent:
		FitForm(frm, false);
		TallLayout(frm, ResultsOK, 0xFFFF, 0xFFFF, 0xFFFF, true);
		FormDraw(frm);
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
		TallLayout(frm, ResultsOK, 0xFFFF, 0xFFFF, 0xFFFF, false);
		FormDraw(frm);
		ResultsDraw();
		return true;
	default:
		break;
	}
	return false;
}

void SEC_FRONT FrontResults(const GameState *g, Boolean net)
{
	gResults = g;
	gResultsNet = net;
	FrmGotoForm(ResultsForm);
}

static FormEventHandlerType *SEC_FRONT Handler(UInt16 formId)
{
	switch (formId) {
	case TitleForm: return TitleHandler;
	case MapListForm: return MapListHandler;
	case NewGameForm: return NewGameHandler;
	case SettingsForm: return SettingsHandler;
	case ResultsForm: return ResultsHandler;
	case LobbyForm: return LobbyHandler;
	case JoinForm: return JoinHandler;
	}
	return NULL;
}

// Scope the theme around both our handler and the OS's native controls.
static Boolean SEC_FRONT FrontEvent(EventType *e)
{
	FormType *frm = FrmGetActiveForm();
	FormEventHandlerType *handler = Handler(FrmGetFormId(frm));
	Boolean handled = false;
	FontID old = FntSetFont(stdFont);
	UInt16 id = 0;

	UiBegin();
	if (e->eType == frmUpdateEvent) {
		switch (FrmGetFormId(frm)) {
		case TitleForm: TitleDraw(frm); break;
		case MapListForm: MapListHandler(e); break;
		case LobbyForm: case JoinForm: FormDraw(frm); LobbyDraw(); break;
		case ResultsForm: FormDraw(frm); ResultsDraw(); break;
		default: FormDraw(frm); break;
		}
		handled = true;
	} else if (handler) handled = handler(e);
	if (!handled) {
		if (e->eType == ctlEnterEvent) id = e->data.ctlEnter.controlID;
		else if (e->eType == ctlSelectEvent) id = e->data.ctlSelect.controlID;
		else if (e->eType == ctlRepeatEvent) id = e->data.ctlRepeat.controlID;
		if (id && id == Primary(frm)) UiColour(UIObjectFill, UI_GOLD);
		FrmHandleEvent(frm, e);
	}
	UiEnd();
	FntSetFont(old);
	return true;
}

FormEventHandlerType *SEC_FRONT FrontHandler(UInt16 formId)
{
	switch (formId) {
	case TitleForm: case MapListForm: case NewGameForm: case SettingsForm:
	case ResultsForm: case LobbyForm: case JoinForm: return FrontEvent;
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
