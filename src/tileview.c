// Tile view: draws the map with the art from res/art and scrolls it by
// pixels. All drawing is in the screen's native pixels, so a double density
// screen gets 16-pixel tiles and the others 8-pixel tiles.
//
// The art comes in three bitmap families (see tools/art2palm.py): terrain,
// sprites and sprite masks, each a grid ART_COLS cells wide. Each is drawn
// once into an offscreen window in the screen's format, a sheet, and the
// OS picks the family member for the screen's depth and density. To scroll,
// the pixels already on the screen are moved with WinScrollRectangle and
// only the strip that was uncovered is drawn from the terrain sheet.
//
// Which terrain cell a square shows depends on its neighbours (tilepick.c),
// so a changed square redraws the squares round it too.
//
// The tanks, builder, shells, explosions, wrecks and the gunsight are
// drawn over the tiles each frame the game ticked, and erased next time by
// drawing the tiles under them again. The view follows the tank.
//
// Under the view is the status panel (panel.c), then along the bottom a
// row of buttons: the five builder actions, the one chosen shown inverted,
// and the gunsight nearer and further.
#include "palmbolo.h"
#include "screendraw.h"
#include "resource.h"
#include "game/game.h"
#include "game/mapstore.h"
#include "netgame.h"
#include "game/util.h"
#include "tilepick.h"
#include "art.h"

static const GameState *gGame;
static WinHandle gSheet, gSprites, gMasks;
static UInt8 gShift = 5;        // world units to view pixels
// Rectangles drawn over the tiles last frame, to erase next frame: room
// for every explosion, wreck, tank, builder and shell and the gunsight.
#define kMaxRects (GAME_MAX_EXPL + GAME_MAX_TKEXPL + 2 * GAME_MAX_TANKS + GAME_MAX_SHELLS + 1)
static RectangleType gOld[kMaxRects];
static UInt16 gNumOld;
// Pills and bases as last drawn: x, y, owner, armour and inTank (pills).
static UInt8 gPillLook[MAP_MAX_PILLS][5], gBaseLook[MAP_MAX_BASES][2];
static UInt16 gShownDeaths = 0xFFFF;
// The button row: its height and top, the action shown chosen, and whether
// it needs drawing. The panel is between the view and the row.
#define kButtons 7
static Coord gBarH, gBarY, gPanelH, gLine;
static UInt8 gAction, gBarShown = 0xFF;
static Boolean gWasCovered;

static void SEC_UI CentreOnTank(void);
static void SEC_UI NearBases(void);
static void SEC_UI CellRect(UInt16 cell, RectangleType *r);
static void SEC_UI PickClear(void);
static Coord gTs = 8;           // tile size in pixels
static Coord gW, gH;            // view size
static Int32 gVx, gVy;          // world pixel at the top left of the view
static UInt16 gTps = 100;
static Boolean gInputClosed;
static Boolean gPins;
static Boolean gHiRes;
static UInt16 gOldCoord;

static void SEC_UI Fill(Coord x, Coord y, Coord w, Coord h, IndexedColorType c)
{
	RectangleType r;

	r.topLeft.x = x;
	r.topLeft.y = y;
	r.extent.x = w;
	r.extent.y = h;
	if (ScreenNewWin()) {
		WinSetForeColor(c);
		WinDrawRectangle(&r, 0);
	} else if (c) {
		WinDrawRectangle(&r, 0);
	} else {
		WinEraseRectangle(&r, 0);
	}
}

// Colour indexes, looked up while the screen is the draw window, since an
// offscreen window may not have the screen's palette.
static IndexedColorType gWhite, gBlack;

static void SEC_COLD LookupColours(void)
{
	gWhite = ScreenIndex(255, 255, 255);
	gBlack = ScreenIndex(0, 0, 0);
}

// The index with every bit set, which the sprite masks are drawn in so
// winMask clears a pixel to index 0 (white). Black can have another index
// (0xD7 on the 8-bit palette), so it isn't looked up.
static IndexedColorType SEC_UI AllOnes(void)
{
	UInt16 d = ScreenDepth();

	return (IndexedColorType)(d >= 8 ? 0xFF : (1 << d) - 1);
}

// The mask sheet in a 1-bit window (OS 3.5 and later), and the bitmaps
// under it. gMask1 says the mask is 1-bit and is drawn with the colours
// from AllOnes.
static BitmapType *gMaskBmp, *gMaskBmpV3;
static Boolean gMask1;

static void SEC_UI FreeMask1(WinHandle win)
{
	if (win)
		WinDeleteWindow(win, false);
	if (gMaskBmpV3)
		BmpDelete(gMaskBmpV3);
	if (gMaskBmp)
		BmpDelete(gMaskBmp);
	gMaskBmp = gMaskBmpV3 = NULL;
}

// A 1-bit offscreen window w by ht native pixels, at double density on a
// hi-res screen. NULL if there is no memory.
static WinHandle SEC_COLD NewWindow1(Coord w, Coord ht)
{
	UInt16 err = 0;
	WinHandle win = NULL;

	gMaskBmp = BmpCreate(w, ht, 1, NULL, &err);
	if (gMaskBmp && gHiRes && !ScreenSony()) {
		gMaskBmpV3 = (BitmapType *)BmpCreateBitmapV3(gMaskBmp, kDensityDouble,
			BmpGetBits(gMaskBmp), NULL);
		if (gMaskBmpV3)
			win = WinCreateBitmapWindow(gMaskBmpV3, &err);
	} else if (gMaskBmp) {
		win = WinCreateBitmapWindow(gMaskBmp, &err);
	}
	if (!win) {
		LOG(("1-bit window %d x %d: none (err %x)", (Int16)w, (Int16)ht, err));
		FreeMask1(NULL);
	}
	return win;
}

// Draws bitmap family id into a new offscreen window big enough for cells
// cells, 1-bit if oneBit, else in the screen's format. Returns NULL if
// there is no memory for it.
static BlitWin gSheetB, gSprB, gMaskB, gViewB;  // sheets and the view, drawn into directly
static const RectangleType *gClipB; // DrawRegion's clip while it draws
// The view's tiles without sprites, kept when the view is drawn directly
// at 2 bits: tiles are drawn into it and copied to the view, and a sprite
// is erased by copying back what was under it. gDstB is where tiles and
// mines go, gBgB or the view.
// gBgB.bits is the view's top left inside gBgMem, which has room for
// kBgMargin rows more above and below (none if that didn't fit). Rows run
// on into each other, so a scroll moves gBgB.bits rather than the pixels,
// a byte for 4 pixels sideways or a row, until it runs out of room and the
// pixels move back to the middle.
#ifndef kBgMargin
#define kBgMargin 32
#endif
static BlitWin gBgB;
static UInt8 *gBgMem;
static UInt16 gBgRoom;              // gBgB.bits can be up to this far into gBgMem
static UInt16 gBgO;                 // and is this far
static BlitWin *gDstB = &gViewB;
static Boolean gBgValid;
// The scroll moved gBgB only: DrawRegion leaves the view alone and the
// frame copies all of gBgB to the view once the scroll is done.
static Boolean gBgMoved;
// The frame is drawn offscreen (compose.c) and copied to the screen at the
// end; gCompValid once the window holds the whole view.
static Boolean gComposing, gCompValid;

// b, if not NULL, gets the sheet's pixels for BlitCell.
static WinHandle SEC_COLD LoadSheet(UInt16 id, UInt16 cells, Boolean oneBit, BlitWin *b)
{
	UInt16 err = 0;
	WinHandle win, old;
	MemHandle h;
	Coord w = ART_COLS * gTs, ht = (Coord)((cells + ART_COLS - 1) / ART_COLS) * gTs;

	if (oneBit)
		win = NewWindow1(w, ht);
	else
		win = WinCreateOffscreenWindow(w, ht, gHiRes ? nativeFormat : screenFormat, &err);
	h = DmGetResource(bitmapRsc, ScreenSony() ? id + 10 : id);
	if (!win || !h) {
		if (oneBit)
			FreeMask1(win);
		else if (win)
			WinDeleteWindow(win, false);
		if (h)
			DmReleaseResource(h);
		LOG(("art %u: no window (err %x) or resource", id, err));
		return NULL;
	}
	if (b)
		BlitFind(win, b);
	old = WinSetDrawWindow(win);
	if (ScreenNewWin()) {
		// A 1-bit member (the masks) is drawn in these colours.
		IndexedColorType fg = WinSetForeColor(oneBit ? 1 : AllOnes()),
			bg = WinSetBackColor(0);

		WinDrawBitmap((BitmapType *)MemHandleLock(h), 0, 0);
		WinSetForeColor(fg);
		WinSetBackColor(bg);
	} else {
		WinDrawBitmap((BitmapType *)MemHandleLock(h), 0, 0);
	}
	MemHandleUnlock(h);
	DmReleaseResource(h);
	WinSetDrawWindow(old);
#ifdef DEBUG
	if (ScreenOs40()) {
		BitmapType *bmp = WinGetBitmap(win);
		Coord bw = 0, bh = 0;
		UInt16 rb = 0;

		BmpGetDimensions(bmp, &bw, &bh, &rb);
		LOG(("art %u: %d x %d depth %u, %lu bytes", id, (Int16)bw, (Int16)bh,
			BmpGetBitDepth(bmp), (UInt32)rb * bh));
	}
#endif
	return win;
}

static void SEC_COLD FreeSheets(void)
{
	if (gSheet)
		WinDeleteWindow(gSheet, false);
	if (gSprites)
		WinDeleteWindow(gSprites, false);
	if (gMask1)
		FreeMask1(gMasks);
	else if (gMasks)
		WinDeleteWindow(gMasks, false);
	gSheet = gSprites = gMasks = NULL;
	gMask1 = false;
	gSheetB.bits = gSprB.bits = gMaskB.bits = gViewB.bits = NULL;
	if (gBgMem)
		MemPtrFree(gBgMem);
	gBgMem = gBgB.bits = NULL;
	gBgValid = false;
	CompFree();
	gCompValid = false;
}

// Copies mask cell 0 from the 1-bit sheet with winMask onto a cell filled
// with AllOnes, as DrawSprite does, and checks a set mask pixel came out 0
// and a clear one stayed. Says whether this OS converts a 1-bit source by
// the fore and back colours.
static Boolean SEC_COLD Mask1Works(void)
{
	UInt16 err;
	WinHandle tmp = WinCreateOffscreenWindow(gTs, gTs, gHiRes ? nativeFormat : screenFormat, &err);
	WinHandle old;
	RectangleType r;
	Coord x, y, sx = -1, sy = -1, cx = -1, cy = -1;
	IndexedColorType fg, bg, one = AllOnes();
	Boolean ok = false;

	if (!tmp)
		return false;
	old = WinSetDrawWindow(gMasks);
	for (y = 0; y < gTs; y++)
		for (x = 0; x < gTs; x++) {
			if (WinGetPixel(x, y))
				sx = x, sy = y;
			else
				cx = x, cy = y;
		}
	WinSetDrawWindow(tmp);
	CellRect(0, &r);
	fg = WinSetForeColor(one);
	bg = WinSetBackColor(0);
	WinDrawRectangle(&r, 0);
	WinCopyRectangle(gMasks, tmp, &r, 0, 0, winMask);
	if (sx >= 0 && cx >= 0)
		ok = WinGetPixel(sx, sy) == 0 && WinGetPixel(cx, cy) == one;
	WinSetForeColor(fg);
	WinSetBackColor(bg);
	WinSetDrawWindow(old);
	WinDeleteWindow(tmp, false);
	return ok;
}

// Returns false if a sheet couldn't be made.
static Boolean SEC_COLD LoadSheets(void)
{
	FreeSheets();
	LookupColours();
	gSheet = LoadSheet(ArtTerrainBmp, ART_TERRAIN_CELLS, false, &gSheetB);
	NetService();
	gSprites = LoadSheet(ArtSpriteBmp, ART_SPRITE_CELLS, false, &gSprB);
	NetService();
	// The mask is 1-bit where the OS can blit that onto the screen's depth,
	// else in the screen's format.
	if (ScreenNewWin() && ScreenDepth() > 1) {
		gMasks = LoadSheet(ArtMaskBmp, ART_MASK_CELLS, true, NULL);
		gMask1 = gMasks != NULL;
		if (gMask1 && !Mask1Works()) {
			LOG(("1-bit mask doesn't blit here, using a full depth one"));
			FreeMask1(gMasks);
			gMasks = NULL;
			gMask1 = false;
		}
	}
	if (!gMasks)
		gMasks = LoadSheet(ArtMaskBmp, ART_MASK_CELLS, false, &gMaskB);
	LOG(("mask sheet %s", gMask1 ? "1-bit" : "screen depth"));
	return gSheet && gSprites && gMasks;
}

// Native pixel drawing is on for as long as the view draws.
static void SEC_UI NativeOn(void)
{
	ScreenNative(true);
	if (gHiRes && !ScreenSony())
		gOldCoord = WinSetCoordinateSystem(kCoordinatesNative);
}

static void SEC_UI NativeOff(void)
{
	if (gHiRes && !ScreenSony())
		WinSetCoordinateSystem(gOldCoord);
	ScreenNative(false);
}

static Boolean SEC_UI PinsAvailable(void)
{
	UInt32 v;

	return gPins && FtrGet(pinCreator, pinFtrAPIVersion, &v) == errNone &&
		v >= pinAPIVersion1_0;
}

// Tells the view the device has a collapsible input area, before any game.
void SEC_UI ViewPins(Boolean on)
{
	gPins = on;
}

// Sets the input area for a new form, which has its own policy, even if
// it is already in that state.
void SEC_UI ViewFormInputArea(Boolean closed)
{
	gInputClosed = !closed;
	ViewSetInputArea(closed);
}

// Lets the user open the input area from the status bar while it stays
// closed.
void SEC_UI ViewInputTrigger(void)
{
	if (PinsAvailable())
		PINSetInputTriggerState(pinInputTriggerEnabled);
}

// Opens or closes the input area. Returns true if it asked the system to.
Boolean SEC_UI ViewSetInputArea(Boolean closed)
{
	FormType *frm = FrmGetActiveForm();

	if (!PinsAvailable() || !frm || closed == gInputClosed)
		return false;
#if defined(DEBUG) && defined(KEEP_INPUT_AREA)
	if (closed)
		return false;
#endif
	if (closed) {
		WinSetConstraintsSize(FrmGetWindowHandle(frm), 160, 240, 240, 160, 160, 240);
		FrmSetDIAPolicyAttr(frm, frmDIAPolicyCustom);
		PINSetInputTriggerState(pinInputTriggerDisabled);
		PINSetInputAreaState(pinInputAreaClosed);
	} else {
		PINSetInputAreaState(pinInputAreaUser);
		PINSetInputTriggerState(pinInputTriggerEnabled);
		FrmSetDIAPolicyAttr(frm, frmDIAPolicyStayOpen);
	}
	gInputClosed = closed;
	return true;
}

// Takes the window size after the input area changed and keeps the centre.
void SEC_COLD ViewResized(void)
{
	FormType *frm = FrmGetActiveForm();
	RectangleType r;
	Coord w, h;
	Int32 cx = gVx + gW / 2, cy = gVy + gH / 2;

	if (frm) {
		ScreenDisplayRect(&r);
		WinSetBounds(FrmGetWindowHandle(frm), &r);
	}
	NativeOn();
	WinGetDisplayExtent(&w, &h);
	{
		FontID old = FntSetFont(stdFont);

		gLine = FntLineHeight();
		// Tall enough for an icon with a frame round it.
		gBarH = gLine + 2;
		if (gBarH < gTs + 5)
			gBarH = gTs + 5;
		FntSetFont(old);
	}
	if (ScreenSony())
		gBarH = (gBarH + 1) & ~1;
	NativeOff();
	gW = w;
	gPanelH = PanelLayout(w);
	gH = h - gBarH - gPanelH;
	gBarY = gH + gPanelH;
	gBarShown = 0xFF;
	gVx = cx - gW / 2;
	gVy = cy - gH / 2;
}

static void SEC_UI ClampView(void)
{
	Int32 maxX = (Int32)MAP_SIZE * gTs - gW, maxY = (Int32)MAP_SIZE * gTs - gH;

	if (gVx > maxX)
		gVx = maxX;
	if (gVy > maxY)
		gVy = maxY;
	if (gVx < 0)
		gVx = 0;
	if (gVy < 0)
		gVy = 0;
}

Boolean SEC_COLD ViewOpen(const DeviceInfo *d, const GameState *game)
{
	Boolean ok;

	// Masking needs an indexed screen; a 16-bit one is refused.
	if (ScreenDepth() > 8) {
		LOG(("screen depth %u: not drawn", ScreenDepth()));
		FrmCustomAlert(ViewAlert, "PalmBolo can't draw on a 16-bit screen.", "", "");
		return false;
	}
	gGame = game;
	NearBases();
	PickClear();
	gPins = d->pins;
	gHiRes = ScreenHiRes();
	gTs = gHiRes ? 16 : 8;
	gTps = d->ticksPerSecond ? d->ticksPerSecond : 100;
	gW = gH = 0;
	ViewFormInputArea(true);
	ViewResized();
	gShift = gTs == 16 ? 4 : 5;
	NativeOn();
#ifdef DEBUG
	{
		UInt32 t0 = TimGetTicks(), t1;

		ok = LoadSheets();
		t1 = TimGetTicks();
		PanelOpen(d, game);
		LOG(("view open: art sheets %lu sys ticks, panel and overview %lu", t1 - t0,
			TimGetTicks() - t1));
	}
#else
	ok = LoadSheets();
	PanelOpen(d, game);
#endif
	NativeOff();
	if (!ok) {
		FreeSheets();
		FrmCustomAlert(ViewAlert, "Not enough memory for the art.", "", "");
		return false;
	}
	gNumOld = 0;
	gShownDeaths = 0xFFFF;
	gWasCovered = false;
	if (gSheetB.bits)
		BlitFind(WinGetDrawWindow(), &gViewB);
	CentreOnTank();
	return true;
}

void SEC_COLD ViewClose(void)
{
	ViewSetInputArea(false);
	FreeSheets();
	PanelClose();
}

static void SEC_UI CellRect(UInt16 cell, RectangleType *r)
{
	r->topLeft.x = (Coord)(cell % ART_COLS) * gTs;
	r->topLeft.y = (Coord)(cell / ART_COLS) * gTs;
	r->extent.x = gTs;
	r->extent.y = gTs;
}

#ifdef DEBUG
static Boolean gSkipBlit;       // ViewTimeFull: time the tile picking alone
#endif

// Set by DrawRegion when cells can be copied a byte at a time: drawn
// directly at 2 bits from a 2-bit sheet, 8 pixel tiles, and the clip's
// left and right on 4 pixel steps.
static Boolean gFastClip;

// CopyTile for gFastClip and x on a 4 pixel step: the bytes BlitCell
// would copy, without the call into the draw segment.
static void SEC_UI FastCell(UInt16 cell, Coord x, Coord y)
{
	const RectangleType *c = gClipB;
	Int16 r0 = 0, r1 = 8, b0 = 0, b1 = 2, r;
	UInt16 srb = gSheetB.rowBytes, drb = gDstB->rowBytes;
	const UInt8 *s;
	UInt8 *d;

	if (c->topLeft.y > y)
		r0 = c->topLeft.y - y;
	if (c->topLeft.y + c->extent.y < y + 8)
		r1 = c->topLeft.y + c->extent.y - y;
	if (c->topLeft.x > x)
		b0 = (c->topLeft.x - x) >> 2;
	if (c->topLeft.x + c->extent.x < x + 8)
		b1 = (c->topLeft.x + c->extent.x - x) >> 2;
	if (b0 >= b1 || r0 >= r1)
		return;
	s = gSheetB.bits + (UInt16)(((cell >> 4) << 3) + r0) * srb + ((cell & 15) << 1) + b0;
	d = gDstB->bits + (UInt16)(y + r0) * drb + (x >> 2) + b0;
	if (b1 - b0 == 2)
		for (r = r0; r < r1; r++, s += srb, d += drb) {
			d[0] = s[0];
			d[1] = s[1];
		}
	else
		for (r = r0; r < r1; r++, s += srb, d += drb)
			d[0] = s[0];
}

static void SEC_UI CopyTile(UInt16 cell, Int32 tx, Int32 ty)
{
	RectangleType src;
	Coord x, y;

#ifdef DEBUG
	if (gSkipBlit)
		return;
#endif

	// Squares and view pixels fit 16 bits (255 * 16), so a 16-bit multiply
	// does; a 32-bit one is a library call on the 68000.
	x = (Coord)((Int16)tx * gTs - (Int16)gVx);
	y = (Coord)((Int16)ty * gTs - (Int16)gVy);
	if (gFastClip && !(x & 3)) {
		FastCell(cell, x, y);
		return;
	}
	CellRect(cell, &src);
	if (gClipB && gSheetB.bits) {
		BlitCell(&gSheetB, src.topLeft.x, src.topLeft.y, gDstB, x, y, gClipB);
		return;
	}
	WinCopyRectangle(gSheet, WinGetDrawWindow(), &src, x, y, winPaint);
}

// Draws sprite cell with its top left at view pixel (x, y). The mask
// (AllOnes where the sprite is) clears those pixels to index 0 with
// winMask, then the sprite is added with winOverlay; this relies on index 0
// being white, as in the Palm palettes, and on depths of 8 bits or fewer.
// A 1-bit mask is turned into AllOnes and 0 by the fore and back colours.
// Tanks share a mask for each heading on land and on a boat.
static void SEC_UI DrawSprite(UInt16 cell, Coord x, Coord y)
{
	RectangleType src;
	UInt16 m = cell < ART_TANK_CELLS ? cell % 32 : cell - ART_TANK_CELLS + 32;

	if (!gSprites || !gMasks)
		return;
	CellRect(m, &src);
	if (gClipB && gSprB.bits && gMaskB.bits && !gMask1) {
		RectangleType s;

		CellRect(cell, &s);
		BlitSprite(&gMaskB, src.topLeft.x, src.topLeft.y, &gSprB, s.topLeft.x, s.topLeft.y,
			gDstB, x, y, gClipB);
		return;
	}
	if (gMask1) {
		IndexedColorType fg = WinSetForeColor(AllOnes()), bg = WinSetBackColor(0);

		WinCopyRectangle(gMasks, WinGetDrawWindow(), &src, x, y, winMask);
		WinSetForeColor(fg);
		WinSetBackColor(bg);
	} else {
		WinCopyRectangle(gMasks, WinGetDrawWindow(), &src, x, y, winMask);
	}
	CellRect(cell, &src);
	WinCopyRectangle(gSprites, WinGetDrawWindow(), &src, x, y, winOverlay);
}

// 0 own, 1 allied, 2 enemy or nobody's, for the art's owner order.
static UInt16 SEC_UI OwnerArt(uint8_t owner)
{
	UInt8 kind = game_owner_kind(gGame, owner, game_viewer());

	return kind == OWN_SELF ? 0 : kind == OWN_ALLY ? 1 : 2;
}

static UInt16 SEC_UI PillTile(const MapPill *p)
{
	UInt16 a = p->armour, stage;

	if (a == 0)
		return AT_PILL_DEAD;
	// Four looks from whole to badly damaged.
	stage = a >= R_PILL_MAX_ARMOUR ? 0 : a >= 10 ? 1 : a >= 5 ? 2 : 3;
	return AT_PILL_OWN_0 + OwnerArt(p->owner) * 4 + stage;
}

static UInt16 SEC_UI BaseTile(const MapBase *b)
{
	UInt8 kind = game_owner_kind(gGame, b->owner, game_viewer());

	return kind == OWN_NEUTRAL ? AT_BASE_NEUTRAL : AT_BASE_OWN + OwnerArt(b->owner);
}

// Bases count as road to the squares round them, as in WinBolo.
// Bit y of gNearRow is set if a base lies on row y - 1, y or y + 1, the
// same for columns, so most squares skip the search below. Set by
// ViewOpen; the bases don't move.
static UInt8 gNearRow[32], gNearCol[32];

static void SEC_UI NearBases(void)
{
	const MapInfo *m = &gGame->map;
	UInt16 i;
	Int16 j;

	MemSet(gNearRow, sizeof(gNearRow), 0);
	MemSet(gNearCol, sizeof(gNearCol), 0);
	for (i = 0; i < m->numBases; i++)
		for (j = -1; j <= 1; j++) {
			UInt8 y = (UInt8)(m->bases[i].y + j), x = (UInt8)(m->bases[i].x + j);

			gNearRow[y >> 3] |= (UInt8)(1 << (y & 7));
			gNearCol[x >> 3] |= (UInt8)(1 << (x & 7));
		}
}

static void SEC_UI BasesAsRoad(Int32 tx, Int32 ty, uint8_t n[8])
{
	const MapInfo *m = &gGame->map;
	UInt16 i;
	UInt8 x = (UInt8)tx, y = (UInt8)ty;

	if (!((gNearRow[y >> 3] >> (y & 7)) & (gNearCol[x >> 3] >> (x & 7)) & 1))
		return;
	for (i = 0; i < m->numBases; i++) {
		Int16 dx = (Int8)(UInt8)(m->bases[i].x - tx), dy = (Int8)(UInt8)(m->bases[i].y - ty);

		if (!m->bases[i].active || dx < -1 || dx > 1 || dy < -1 || dy > 1 || (dx == 0 && dy == 0))
			continue;
		n[dy < 0 ? TP_A + dx : dy > 0 ? TP_B + dx : dx < 0 ? TP_L : TP_R] = TERRAIN_ROAD;
	}
}

// True when tiles go through gBgB: it fits the view and the view is drawn
// directly (BgFit only makes it then).
static Boolean SEC_UI BgUse(void)
{
	return gBgB.bits && gBgB.w == gW && gBgB.h == gH && gViewB.bits &&
		WinGetDrawWindow() == gViewB.win;
}

// Makes gBgB for the view's size if the view, the tiles and the sprites
// are all drawn directly at 2 bits. Its contents are only good once the
// whole view has been drawn into it. The width is a multiple of 8 so rows
// stay word aligned for the 68000 while gBgO is even.
static void SEC_UI BgFit(void)
{
	UInt16 rb = (UInt16)gW >> 2;

	if (gBgB.bits && gBgB.w == gW && gBgB.h == gH)
		return;
	if (gBgMem)
		MemPtrFree(gBgMem);
	gBgMem = gBgB.bits = NULL;
	gBgValid = false;
#ifdef NO_BG
	return;
#endif
	if (!gViewB.bits || gViewB.depth != 2 || !gSheetB.bits || !gSprB.bits || !gMaskB.bits ||
	    gMask1 || (gW & 7) || gW > gViewB.w || gH > gViewB.h || gW <= 0 || gH <= 0)
		return;
	gBgRoom = (UInt16)(2 * kBgMargin * rb);
	gBgMem = MemPtrNew((UInt32)rb * gH + gBgRoom);
	if (!gBgMem) {
		gBgRoom = 0;
		gBgMem = MemPtrNew((UInt32)rb * gH);
		if (!gBgMem)
			return;
	}
	gBgO = gBgRoom / 2;
	gBgB.rowBytes = rb;
	gBgB.bits = gBgMem + gBgO;
	gBgB.depth = 2;
	gBgB.w = gW;
	gBgB.h = gH;
	gBgB.win = NULL;
}

// Moves gBgB's top left d bytes on in gBgMem (a byte is 4 pixels, a row
// rowBytes), as ShiftBits would move the pixels back. gBgO stays even so
// copies from gBgB move longs: past the room, or when the move would make
// it odd, the pixels move instead. What the move uncovers is left for the caller to draw.
static void SEC_UI BgMove(Int32 d)
{
	// The view is under 64 KB, so a 16-bit multiply does (a 32-bit one is
	// a library call).
	Int32 size = (Int32)(UInt16)(gBgB.rowBytes * (UInt16)gH);
	Int32 o = (Int32)gBgO + d, to, n = size, from = gBgO;

	if (o >= 0 && o <= (Int32)gBgRoom && (o & 1))
		o += o > (Int32)(gBgRoom / 2) ? -1 : 1;
	if (o < 0 || o > (Int32)gBgRoom || (o - d) != (Int32)gBgO) {
		// The old pixels go where the new top left lands: in the middle
		// when out of room, else a byte off to keep it even.
		// Bytes that would land outside gBgMem are outside the new view.
		if (o < 0 || o > (Int32)gBgRoom)
			o = (Int32)(gBgRoom / 2);
		if (o > (Int32)gBgRoom)
			o = gBgRoom;
		to = o - d;
		if (to < 0) {
			from -= to;
			n += to;
			to = 0;
		}
		if (to + n > (Int32)gBgRoom + size)
			n = (Int32)gBgRoom + size - to;
		if (n > 0)
			BlitMove(gBgMem + to, gBgMem + from, (UInt32)n);
	}
	gBgO = (UInt16)o;
	gBgB.bits = gBgMem + gBgO;
}

// Copies r (view pixels, inside the view) from gBgB to the view, in whole
// bytes.
static void SEC_UI BgToView(const RectangleType *r)
{
	UInt16 b0, n, vrb = gViewB.rowBytes, brb = gBgB.rowBytes;
	const UInt8 *s;
	UInt8 *d;

	if (r->extent.x <= 0 || r->extent.y <= 0)
		return;
	b0 = (UInt16)r->topLeft.x >> 2;
	n = (UInt16)((r->topLeft.x + r->extent.x + 3) >> 2) - b0;
	s = gBgB.bits + (UInt32)r->topLeft.y * brb + b0;
	d = gViewB.bits + (UInt32)r->topLeft.y * vrb + b0;
	if (n == brb && brb == vrb) {
		BlitMove(d, s, (UInt32)n * r->extent.y);
		return;
	}
	BlitRows(d, vrb, s, brb, n, (UInt16)r->extent.y);
}

// The art cell picked for each map square drawn lately, by square modulo
// kPick on each axis, so the squares in and round the view stay picked as
// it scrolls. gPickTag holds the square (y << 8 | x), and a cell of 255 is
// none. A terrain change forgets the squares round it, and a full frame all.
#define kPick 32
static UInt16 gPickTag[kPick * kPick];
static UInt8 gPickCell[kPick * kPick];

static void SEC_UI PickClear(void)
{
	MemSet(gPickCell, sizeof(gPickCell), 255);
}

// Forgets the squares whose pick square (x, y) is a neighbour of.
static void SEC_UI PickForget(UInt8 x, UInt8 y)
{
	Int16 i, j;

	for (j = -1; j <= 1; j++)
		for (i = -1; i <= 1; i++)
			gPickCell[(((UInt8)(y + j) & (kPick - 1)) << 5) | ((UInt8)(x + i) & (kPick - 1))] = 255;
}

// Draws map square (tx, ty) without the pill, base or start on it. up, row
// and down are map rows ty - 1, ty and ty + 1.
static void SEC_UI DrawTerrain(const uint8_t *up, const uint8_t *row, const uint8_t *down, Int32 tx,
	Int32 ty)
{
	uint8_t t = row[tx], n[8], cell;
	Boolean mine = false;
	UInt16 slot = (UInt16)((((UInt8)ty & (kPick - 1)) << 5) | ((UInt8)tx & (kPick - 1)));
	UInt16 tag = (UInt16)(((UInt16)(UInt8)ty << 8) | (UInt8)tx);

	// A hidden mine the player wasn't shown is drawn without it.
	if (t >= TERRAIN_MINE_FIRST && t <= TERRAIN_MINE_LAST) {
		mine = !gGame->hiddenMines || game_mine_shown(gGame, (uint8_t)tx, (uint8_t)ty);
		t = (uint8_t)(t - TERRAIN_MINE_ADD);
	}
	cell = gPickCell[slot];
	if (cell == 255 || gPickTag[slot] != tag) {
		if (tile_has_variants(t)) {
			tile_neighbours(up, row, down, (uint8_t)tx, n);
			if (tile_bases_matter(t))
				BasesAsRoad(tx, ty, n);
		}
		cell = tile_pick(t, n);
		gPickCell[slot] = cell;
		gPickTag[slot] = tag;
	}
	CopyTile(cell, tx, ty);
	if (mine)
		DrawSprite(AS_MINE, (Coord)((Int16)tx * gTs - (Int16)gVx),
			(Coord)((Int16)ty * gTs - (Int16)gVy));
}

// Draws the part of the view inside r (view pixels) from the map.
static void SEC_UI DrawRegion(const RectangleType *requested)
{
	RectangleType aligned = *requested;
	const RectangleType *r = &aligned;
	// The view and r are never left of or above the map, so a shift
	// divides by the tile size (256 world units over gShift).
	UInt8 ts = (UInt8)(8 - gShift);
	Int32 tx0, tx1, ty0, ty1;
	Int32 tx, ty;
	UInt16 i;
	UInt8 x0, y0, w, h;
	const MapInfo *m = &gGame->map;
	const uint8_t *up, *row, *down;
	Boolean osClip, bg = BgUse();

	if (!gSheet || r->extent.x <= 0 || r->extent.y <= 0)
		return;
	ScreenAlignRect(&aligned);
	if (gComposing && ScreenSony())
		CompAdd(r);
	tx0 = (gVx + r->topLeft.x) >> ts;
	tx1 = (gVx + r->topLeft.x + r->extent.x - 1) >> ts;
	ty0 = (gVy + r->topLeft.y) >> ts;
	ty1 = (gVy + r->topLeft.y + r->extent.y - 1) >> ts;
	if (tx1 > MAP_SIZE - 1)
		tx1 = MAP_SIZE - 1;
	if (ty1 > MAP_SIZE - 1)
		ty1 = MAP_SIZE - 1;
	// Squares tx0 to tx1 and ty0 to ty1 as one unsigned byte compare each.
	x0 = (UInt8)tx0;
	y0 = (UInt8)ty0;
	w = (UInt8)(tx1 - tx0);
	h = (UInt8)(ty1 - ty0);
	if (gSheetB.bits && gViewB.bits && WinGetDrawWindow() == gViewB.win &&
	    r->topLeft.x >= 0 && r->topLeft.y >= 0 && r->topLeft.x + r->extent.x <= gViewB.w &&
	    r->topLeft.y + r->extent.y <= gViewB.h)
		gClipB = r;
	// Direct cells and mines clip themselves, the OS calls need the clip.
	osClip = !gClipB || !gSprB.bits || !gMaskB.bits || gMask1;
	if (osClip)
		WinSetClip(r);
	if (bg)
		gDstB = &gBgB;
	gFastClip = gClipB && gSheetB.bits && gSheetB.depth == 2 && gDstB->depth == 2 && gTs == 8 &&
		!(r->topLeft.x & 3) && !(r->extent.x & 3);
	for (ty = ty0; ty <= ty1; ty++) {
		NetService();
		up = mapstore_row((uint8_t)(ty - 1));
		row = mapstore_row((uint8_t)ty);
		down = mapstore_row((uint8_t)(ty + 1));
		for (tx = tx0; tx <= tx1; tx++) {
			DrawTerrain(up, row, down, tx, ty);
			if (!(tx & 3))
				NetService();
		}
	}
	for (i = 0; i < m->numStarts; i++)
		if ((UInt8)(m->starts[i].y - y0) <= h && (UInt8)(m->starts[i].x - x0) <= w)
			CopyTile(AT_START, m->starts[i].x, m->starts[i].y);
	for (i = 0; i < m->numBases; i++) {
		const MapBase *b = &m->bases[i];

		if ((UInt8)(b->y - y0) <= h && (UInt8)(b->x - x0) <= w && b->active)
			CopyTile(BaseTile(b), b->x, b->y);
	}
	for (i = 0; i < m->numPills; i++) {
		const MapPill *p = &m->pills[i];

		if ((UInt8)(p->y - y0) > h || (UInt8)(p->x - x0) > w || !p->active || p->inTank)
			continue;
		CopyTile(PillTile(p), p->x, p->y);
	}
	gClipB = NULL;
	gFastClip = false;
	if (osClip)
		WinResetClip();
	if (bg) {
		gDstB = &gViewB;
		if (!gBgMoved)
			BgToView(r);
	}
}

static void SEC_UI DrawAll(void)
{
	RectangleType r;

	r.topLeft.x = r.topLeft.y = 0;
	r.extent.x = gW;
	r.extent.y = gH;
	DrawRegion(&r);
	if (gComposing)
		CompAll();
}

// True if the view can scroll dist pixels by moving its bits: drawn
// directly at 2 bits, a whole number of bytes wide, and sideways only by
// whole bytes.
static Boolean SEC_UI ScrollDirect(Boolean horizontal, Coord dist)
{
#ifdef NO_SCROLL
	return false;
#endif
	return gViewB.bits && gViewB.depth == 2 && WinGetDrawWindow() == gViewB.win &&
		gW <= gViewB.w && gH <= gViewB.h && !(gW & 3) && (!horizontal || !(dist & 3));
}

// Moves the view's pixels dist pixels left (fwd) or right, or up (fwd) or
// down, leaving the vacated strip for the caller to draw.
static void SEC_UI ShiftBits(const BlitWin *b, Boolean horizontal, Boolean fwd, Coord dist)
{
	UInt8 *p = b->bits;
	UInt16 rb = b->rowBytes, wb = (UInt16)gW >> 2, y;

	if (horizontal) {
		UInt16 nb = (UInt16)dist >> 2;

		// Rows that fill the whole row bytes move as one block. The ends
		// of each row then hold bytes of the row beside it, which is the
		// vacated strip the caller draws.
		if (wb == rb) {
			UInt32 n = (UInt32)(UInt16)gH * rb - nb;

			if (fwd)
				BlitMove(p, p + nb, n);
			else
				BlitMove(p + nb, p, n);
			return;
		}
		for (y = 0; y < (UInt16)gH; y++, p += rb)
			if (fwd)
				BlitMove(p, p + nb, wb - nb);
			else
				BlitMove(p + nb, p, wb - nb);
		return;
	}
	if (wb == rb) {
		UInt32 n = (UInt32)(gH - dist) * rb;

		if (fwd)
			BlitMove(p, p + (UInt32)dist * rb, n);
		else
			BlitMove(p + (UInt32)dist * rb, p, n);
		return;
	}
	if (fwd) {
		for (y = 0; y < (UInt16)(gH - dist); y++, p += rb)
			BlitMove(p, p + (UInt32)dist * rb, wb);
	} else {
		p += (UInt32)(gH - 1 - dist) * rb;
		for (y = 0; y < (UInt16)(gH - dist); y++, p -= rb)
			BlitMove(p + (UInt32)dist * rb, p, wb);
	}
}

// Moves the view by dx along one axis (horizontal) or dy, redrawing the
// strip that was uncovered.
static void SEC_UI ScrollAxis(Int32 d, Boolean horizontal)
{
	RectangleType view, vacated;
	Int32 old = horizontal ? gVx : gVy, now = old + d, max;
	Coord dist;

	max = (Int32)MAP_SIZE * gTs - (horizontal ? gW : gH);
	if (now < 0)
		now = 0;
	if (now > max)
		now = max;
	d = now - old;
	if (d == 0)
		return;
	dist = (Coord)(d < 0 ? -d : d);
	if (gComposing)
		CompAll();
	if (horizontal)
		gVx = now;
	else
		gVy = now;
	if (dist >= (horizontal ? gW : gH)) {
		DrawAll();
		return;
	}
	if (BgUse() && !ScrollDirect(horizontal, dist)) {
		DrawAll();
		return;
	}
	if (ScrollDirect(horizontal, dist)) {
		vacated.topLeft.x = vacated.topLeft.y = 0;
		vacated.extent.x = gW;
		vacated.extent.y = gH;
		if (horizontal) {
			vacated.extent.x = dist;
			if (d > 0)
				vacated.topLeft.x = gW - dist;
		} else {
			vacated.extent.y = dist;
			if (d > 0)
				vacated.topLeft.y = gH - dist;
		}
		// gBgB moves by its top left. With a whole gBgB, after a sideways
		// move the frame copies all of it to the view once both axes are
		// done (gBgMoved), which costs less than moving the view's bits a
		// byte; an up or down move alone moves the view's rows and
		// DrawRegion copies the strip.
		if (BgUse()) {
			Int32 rows = (Int32)(UInt16)((UInt16)dist * gBgB.rowBytes);

			BgMove(horizontal ? (d > 0 ? dist >> 2 : -(dist >> 2)) : (d > 0 ? rows : -rows));
			if (gBgValid && (horizontal || gBgMoved))
				gBgMoved = true;
			else
				ShiftBits(&gViewB, horizontal, d > 0, dist);
		} else {
			ShiftBits(&gViewB, horizontal, d > 0, dist);
		}
		DrawRegion(&vacated);
		return;
	}
	view.topLeft.x = view.topLeft.y = 0;
	view.extent.x = gW;
	view.extent.y = gH;
	WinResetClip();
	WinScrollRectangle(&view, horizontal ? (d > 0 ? winLeft : winRight) :
		(d > 0 ? winUp : winDown), dist, &vacated);
	DrawRegion(&vacated);
}

static void SEC_UI Remember(Coord x, Coord y, Coord w, Coord h)
{
	if (gNumOld >= kMaxRects)
		return;
	gOld[gNumOld].topLeft.x = x;
	gOld[gNumOld].topLeft.y = y;
	gOld[gNumOld].extent.x = w;
	gOld[gNumOld].extent.y = h;
	if (gComposing)
		CompAdd(&gOld[gNumOld]);
	gNumOld++;
}

static Coord SEC_UI WorldX(uint16_t wx)
{
	return (Coord)(((Int32)wx >> gShift) - gVx);
}

static Coord SEC_UI WorldY(uint16_t wy)
{
	return (Coord)(((Int32)wy >> gShift) - gVy);
}

// Draws sprite cell centred on world point (wx, wy) and notes it for
// erasing. One wholly off the view is skipped.
static void SEC_UI DrawAt(UInt16 cell, uint16_t wx, uint16_t wy)
{
	Coord x = WorldX(wx) - gTs / 2, y = WorldY(wy) - gTs / 2;

	NetService();
	if (x <= -gTs || x >= gW || y <= -gTs || y >= gH)
		return;
	DrawSprite(cell, x, y);
	Remember(x, y, gTs, gTs);
}

static void SEC_UI DrawTank(const Tank *k, UInt16 owner)
{
	if (k->destroyed)
		return;
	DrawAt(AS_TANK_OWN_0 + owner * 32 + (k->boat ? 16 : 0) + util_dir16(k->angle), k->x, k->y);
}

// The builder walking, or under a parachute while he flies in.
static void SEC_UI DrawMan(const Builder *m)
{
	if (m->inTank)
		return;
	if (m->isDead || m->frame == LGM_HELICOPTER_FRAME)
		DrawAt(AS_PARA, m->x, m->y);
	else
		DrawAt(AS_MAN_0 + m->frame % 3, m->x, m->y);
}

static void SEC_UI DrawSprites(void)
{
	const GameState *g = gGame;
	UInt8 vt = game_viewer_tank(g);
	const Tank *k = &g->tanks[vt];
	UInt16 i;
	RectangleType clip;
	Boolean osClip;

	clip.topLeft.x = clip.topLeft.y = 0;
	clip.extent.x = gW;
	clip.extent.y = gH;
	if (gViewB.bits && gSprB.bits && gMaskB.bits && WinGetDrawWindow() == gViewB.win &&
	    gW <= gViewB.w && gH <= gViewB.h)
		gClipB = &clip;
	// Direct sprites clip themselves, the OS calls need the clip.
	osClip = !gClipB || gMask1;
	if (osClip)
		WinSetClip(&clip);

	for (i = 0; i < g->numExpl; i++) {
		Int16 f = R_EXPLOSION_START - g->expl[i].frame;

		DrawAt(AS_EXPL_0 + (f < 0 ? 0 : f > 7 ? 7 : f), g->expl[i].x, g->expl[i].y);
	}
	// A dead tank's wreck flying off: one frame, at the head of its trail.
	for (i = 0; i < g->numTkExpl; i++)
		DrawAt(AS_TKEXPL, g->tkExpl[i].x, g->tkExpl[i].y);
	// Every tank in its own, allied or enemy look, the viewer's on top,
	// and every builder that is out.
	for (i = 0; i < g->numTanks; i++)
		if (i != vt)
			DrawTank(&g->tanks[i], OwnerArt(g->tanks[i].player));
	DrawTank(k, 0);
	for (i = 0; i < g->numTanks; i++)
		DrawMan(&g->men[i]);
	for (i = 0; i < g->numShells; i++)
		DrawAt(AS_SHELL_0 + util_dir16(g->shells[i].angle), g->shells[i].x, g->shells[i].y);
	if (!k->destroyed && k->showSight) {
		uint16_t wx, wy;

		game_gunsight(g, vt, &wx, &wy);
		DrawAt(AS_GUNSIGHT, (uint16_t)(wx + 128), (uint16_t)(wy + 128));
	}
	gClipB = NULL;
	if (osClip)
		WinResetClip();
}

static const Char kButtonText[] = "Tree\0Road\0Wall\0Pill\0Mine\0Near\0Far\0";

// Draws sprite sheet cell (a whole cell, no mask) with its top left at x, y.
static void SEC_UI CopyIcon(UInt16 cell, Coord x, Coord y)
{
	RectangleType src;

	CellRect(cell, &src);
	WinCopyRectangle(gSprites, WinGetDrawWindow(), &src, x, y, winPaint);
}

// Draws a whole sprite sheet cell (an icon) at x, y for the panel.
void SEC_UI ViewIcon(UInt16 cell, Coord x, Coord y)
{
	if (gSprites)
		CopyIcon(cell, x, y);
}

// The button row, when the chosen action changed or on a full redraw. Each
// button is an icon on the panel colour; the chosen one is on a black
// plate and the highlight colour.
// A line shown in place of the button row (network play's "waiting for").
static Char gMsg[48];
static Boolean gMsgOn, gMsgNew;

void SEC_UI ViewMessage(const Char *msg)
{
	if (!msg) {
		if (gMsgOn) {
			gMsgOn = false;
			gMsgNew = true;
		}
		return;
	}
	if (gMsgOn && StrCompare(msg, gMsg) == 0)
		return;
	StrNCopy(gMsg, msg, sizeof(gMsg) - 1);
	gMsg[sizeof(gMsg) - 1] = 0;
	gMsgOn = gMsgNew = true;
}

static void SEC_UI DrawBar(Boolean force)
{
	const Char *p = kButtonText;
	RectangleType r;
	UInt16 i;
	Coord x0, x1, len;
	FontID old;
	IndexedColorType oldBack = 0;

	if (!force && gBarShown == gAction && !gMsgNew)
		return;
	gBarShown = gAction;
	gMsgNew = false;
	old = FntSetFont(stdFont);
	if (ScreenNewWin())
		oldBack = WinSetBackColor(PanelBg());
	Fill(0, gBarY, gW, gBarH, PanelBg());
	Fill(0, gBarY, gW, 1, gBlack);
	Fill(0, gBarY + 1, gW, 1, gWhite);
	if (gMsgOn) {
		len = StrLen(gMsg);
		WinDrawChars(gMsg, len, (gW - FntCharsWidth(gMsg, len)) / 2, gBarY + 2 + (gBarH - 2 - gLine) / 2);
		p = NULL;
	}
	for (i = 0; p && i < kButtons; i++, p += len + 1) {
		Boolean sel = i == gAction;

		x0 = (Coord)((Int32)gW * i / kButtons);
		x1 = (Coord)((Int32)gW * (i + 1) / kButtons);
		len = StrLen(p);
		if (i > 0)
			Fill(x0, gBarY, 1, gBarH, gBlack);
		if (gSprites) {
			Coord iy = gBarY + 2 + (gBarH - 2 - gTs) / 2;

			// The chosen one sits on a black plate.
			if (sel)
				Fill(x0 + 1, gBarY + 2, x1 - x0 - 1, gBarH - 2, gBlack);
			CopyIcon((sel ? AS_ICON_TREE_SEL : AS_ICON_TREE) + i, x0 + (x1 - x0 - gTs) / 2 + 1, iy);
		} else {
			WinDrawChars(p, len, x0 + (x1 - x0 - FntCharsWidth(p, len)) / 2, gBarY + 2);
			if (sel) {
				r.topLeft.x = x0 + 1;
				r.topLeft.y = gBarY + 2;
				r.extent.x = x1 - x0 - 1;
				r.extent.y = gBarH - 2;
				WinInvertRectangle(&r, 0);
			}
		}
	}
	if (ScreenNewWin())
		WinSetBackColor(oldBack);
	FntSetFont(old);
}

void SEC_UI ViewSetAction(UInt8 action)
{
	gAction = action;
}

Int16 SEC_UI ViewTap(Coord x, Coord y, UInt8 *mx, UInt8 *my)
{
	Int32 nx = (Int32)x * gTs / 8, ny = (Int32)y * gTs / 8;

	if (ny >= gBarY) {
		Int16 b = (Int16)(nx * kButtons / (gW > 0 ? gW : 1));

		return b < kButtons ? b : kButtons - 1;
	}
	// The panel, or the overview covering the view, toggles the overview.
	if (ny >= gH || PanelCovers())
		return kTapPanel;
	*mx = (UInt8)((gVx + nx) / gTs);
	*my = (UInt8)((gVy + ny) / gTs);
	return -1;
}

static void SEC_UI Erase(const RectangleType *r);

// Passes squares whose terrain changed to the overview and, unless the
// overview covers the view, redraws their tiles.
static void SEC_UI DrawChanges(Boolean covered)
{
	uint8_t xy[64];
	uint8_t n = game_take_changes(xy, 32), i;
	RectangleType r;

	if (n == 255) {
		PickClear();
		PanelRebuild();
		if (!covered)
			DrawAll();
		return;
	}
	for (i = 0; i < n; i++) {
		PanelSquare(xy[i * 2], xy[i * 2 + 1]);
		PickForget(xy[i * 2], xy[i * 2 + 1]);
	}
	if (covered)
		return;
	// The squares round a change may join it differently now.
	for (i = 0; i < n; i++) {
		r.topLeft.x = (Coord)((Int32)(xy[i * 2] - 1) * gTs - gVx);
		r.topLeft.y = (Coord)((Int32)(xy[i * 2 + 1] - 1) * gTs - gVy);
		r.extent.x = gTs * 3;
		r.extent.y = gTs * 3;
		if (r.topLeft.x > -gTs * 3 && r.topLeft.x < gW && r.topLeft.y > -gTs * 3 && r.topLeft.y < gH)
			Erase(&r);
	}
}

static void SEC_UI DrawSquare(UInt8 x, UInt8 y)
{
	RectangleType r;

	r.topLeft.x = (Coord)((Int32)x * gTs - gVx);
	r.topLeft.y = (Coord)((Int32)y * gTs - gVy);
	r.extent.x = gTs;
	r.extent.y = gTs;
	if (r.topLeft.x > -gTs && r.topLeft.x < gW && r.topLeft.y > -gTs && r.topLeft.y < gH)
		Erase(&r);
}

// Redraws pills and bases that changed hands, took damage or moved since
// last drawn. With all set, just notes how they are now.
static void SEC_UI DrawItemChanges(Boolean all, Boolean covered)
{
	const MapInfo *m = &gGame->map;
	UInt16 i;

	for (i = 0; i < m->numPills; i++) {
		const MapPill *p = &m->pills[i];
		UInt8 *o = gPillLook[i];

		if (o[0] == p->x && o[1] == p->y && o[2] == p->owner && o[3] == p->armour &&
		    o[4] == p->inTank)
			continue;
		PanelItem(o[0], o[1]);
		PanelItem(p->x, p->y);
		if (!all && !covered) {
			DrawSquare(o[0], o[1]);
			DrawSquare(p->x, p->y);
		}
		o[0] = p->x;
		o[1] = p->y;
		o[2] = p->owner;
		o[3] = p->armour;
		o[4] = p->inTank;
	}
	for (i = 0; i < m->numBases; i++) {
		const MapBase *b = &m->bases[i];
		UInt8 look = b->armour > R_BASE_CAPTURE_ARMOUR;

		if (gBaseLook[i][0] == b->owner && gBaseLook[i][1] == look)
			continue;
		PanelItem(b->x, b->y);
		if (!all && !covered)
			DrawSquare(b->x, b->y);
		gBaseLook[i][0] = b->owner;
		gBaseLook[i][1] = look;
	}
}

// Keeps the point halfway between the tank and its gunsight at least a
// quarter of the view from each edge, so both stay in sight. Returns the
// distance to scroll on each axis.
static void SEC_UI Follow(Int32 *dx, Int32 *dy)
{
	UInt8 vt = game_viewer_tank(gGame);
	const Tank *k = &gGame->tanks[vt];
	uint16_t sx, sy;
	Int32 tx, ty, mx = gW / 4, my = gH / 4;

	game_gunsight(gGame, vt, &sx, &sy);
	tx = ((Int32)k->x + ((Int32)(int16_t)(uint16_t)(sx + 128 - k->x) >> 1)) >> gShift;
	ty = ((Int32)k->y + ((Int32)(int16_t)(uint16_t)(sy + 128 - k->y) >> 1)) >> gShift;
	*dx = *dy = 0;
	if (tx - gVx < mx)
		*dx = tx - gVx - mx;
	else if (tx - gVx > gW - mx)
		*dx = tx - gVx - (gW - mx);
	if (ty - gVy < my)
		*dy = ty - gVy - my;
	else if (ty - gVy > gH - my)
		*dy = ty - gVy - (gH - my);
}

static void SEC_UI CentreOnTank(void)
{
	const Tank *k = &gGame->tanks[game_viewer_tank(gGame)];

	gVx = ((Int32)k->x >> gShift) - gW / 2;
	gVy = ((Int32)k->y >> gShift) - gH / 2;
	ClampView();
}

// Squares to redraw this frame, when drawing directly: bit c of row r is
// map square (gDx + c, gDy + r), where (gDx, gDy) is the square at the
// view's top left. Erase marks them and DirtyFlush draws them.
#define kDirtyRows 32
static UInt32 gDirty[kDirtyRows];
static Int16 gDx, gDy;
static Boolean gDirtyOn, gAnyDirty;
static RectangleType gViewClip;

// Marks the squares under c, which lies inside the view.
static void SEC_UI Mark(const RectangleType *c)
{
	Int16 ox = (Int16)gVx & 7, oy = (Int16)gVy & 7, c0, c1, r0, r1, r;
	UInt32 bits;

	if (c->extent.x <= 0 || c->extent.y <= 0)
		return;
	c0 = (ox + c->topLeft.x) >> 3;
	c1 = (ox + c->topLeft.x + c->extent.x - 1) >> 3;
	r0 = (oy + c->topLeft.y) >> 3;
	r1 = (oy + c->topLeft.y + c->extent.y - 1) >> 3;
	bits = (0xFFFFFFFFUL >> (31 - (c1 - c0))) << c0;
	for (r = r0; r <= r1; r++)
		gDirty[r] |= bits;
	gAnyDirty = true;
}

static Boolean SEC_UI Marked(Int16 x, Int16 y)
{
	x -= gDx;
	y -= gDy;
	return x >= 0 && x < 32 && y >= 0 && y < kDirtyRows && ((gDirty[y] >> x) & 1);
}

static Boolean gEraseBg;        // Erase copies from gBgB: sprites only

// Draws the tiles back over r, clipped to the view.
static void SEC_UI Erase(const RectangleType *r)
{
	RectangleType c = *r;

	ScreenAlignRect(&c);
	if (c.topLeft.x < 0) {
		c.extent.x += c.topLeft.x;
		c.topLeft.x = 0;
	}
	if (c.topLeft.y < 0) {
		c.extent.y += c.topLeft.y;
		c.topLeft.y = 0;
	}
	if (c.topLeft.x + c.extent.x > gW)
		c.extent.x = gW - c.topLeft.x;
	if (c.topLeft.y + c.extent.y > gH)
		c.extent.y = gH - c.topLeft.y;
	if (gEraseBg)
		BgToView(&c);
	else if (gDirtyOn)
		Mark(&c);
	else
		DrawRegion(&c);
	if (gComposing)
		CompAdd(&c);
}

// Draws the squares Erase marked, each once, then clears the marks.
static void SEC_UI DirtyFlush(void)
{
	const MapInfo *m = &gGame->map;
	UInt16 r, c, i;

	if (!gAnyDirty)
		return;
	gClipB = &gViewClip;
	for (r = 0; r < kDirtyRows; r++) {
		UInt32 bits = gDirty[r];
		Int16 ty = gDy + (Int16)r;
		const uint8_t *up, *row, *down;

		NetService();
		if (!bits)
			continue;
		up = mapstore_row((uint8_t)(ty - 1));
		row = mapstore_row((uint8_t)ty);
		down = mapstore_row((uint8_t)(ty + 1));
		for (c = 0; bits; c++, bits >>= 1) {
			if (bits & 1)
				DrawTerrain(up, row, down, gDx + (Int16)c, ty);
			if (!(c & 3))
				NetService();
		}
	}
	for (i = 0; i < m->numStarts; i++)
		if (Marked(m->starts[i].x, m->starts[i].y))
			CopyTile(AT_START, m->starts[i].x, m->starts[i].y);
	for (i = 0; i < m->numBases; i++) {
		const MapBase *b = &m->bases[i];

		if (b->active && Marked(b->x, b->y))
			CopyTile(BaseTile(b), b->x, b->y);
	}
	for (i = 0; i < m->numPills; i++) {
		const MapPill *p = &m->pills[i];

		if (p->active && !p->inTank && Marked(p->x, p->y))
			CopyTile(PillTile(p), p->x, p->y);
	}
	gClipB = NULL;
	for (r = 0; r < kDirtyRows; r++)
		gDirty[r] = 0;
	gAnyDirty = false;
}

// Starts a frame's marks: on when the view, the art and the sprites are
// all drawn directly, with 8 pixel tiles, and the view fits the marks.
static void SEC_UI DirtyStart(void)
{
	gDirtyOn = !BgUse() && gViewB.bits && gSheetB.bits && gSprB.bits && gMaskB.bits && !gMask1 && gTs == 8 &&
		WinGetDrawWindow() == gViewB.win && gW <= gViewB.w && gH <= gViewB.h &&
		gW <= 8 * 30 && gH <= 8 * (kDirtyRows - 2);
#ifdef NO_DIRTY
	gDirtyOn = false;
#endif
	gDx = (Int16)(gVx >> 3);
	gDy = (Int16)(gVy >> 3);
	gViewClip.topLeft.x = gViewClip.topLeft.y = 0;
	gViewClip.extent.x = gW;
	gViewClip.extent.y = gH;
}

#ifdef DRAW_PARTS
// Debug: where a frame's drawing goes, summed over 100 frames.
static UInt32 gPart[6], gPartT;
static UInt16 gPartN;
#define PART(n) (gPart[n] += TimGetTicks() - gPartT, gPartT = TimGetTicks())
#else
#define PART(n) ((void)0)
#endif

void SEC_UI ViewFrame(Boolean full, Boolean ticked)
{
	Int32 dx, dy;
	UInt16 i;
	Boolean covered = PanelCovers();
	const Tank *k = &gGame->tanks[game_viewer_tank(gGame)];
	WinHandle scr = NULL;
#ifdef DEBUG
	UInt32 t0 = TimGetTicks();
#endif

	if (!full && !ticked) {
		// A new line can come while nothing moves.
		if (gMsgNew) {
			NativeOn();
			DrawBar(false);
			NativeOff();
		}
		return;
	}
#ifdef DRAW_PARTS
	gPartT = TimGetTicks();
#endif
	NativeOn();
	// On OS 3.5 and later the view is drawn offscreen when there's room.
	if (full && !covered && !gViewB.bits && !CompFit(gW, gH))
		gCompValid = false;
	if (CompWin() && !covered) {
		if (!gCompValid)
			full = true;
		scr = WinSetDrawWindow(CompWin());
		CompBegin();
		gComposing = true;
	}
	if (!full) {
		DirtyStart();
		gEraseBg = BgUse() && gBgValid;
		if (!covered)
			for (i = 0; i < gNumOld; i++)
				Erase(&gOld[i]);
		gEraseBg = false;
		PART(0);
		DrawChanges(covered);
		DrawItemChanges(false, covered);
		DirtyFlush();
		gDirtyOn = false;
		PART(1);
		if (k->deaths != gShownDeaths) {
			full = true;
		} else if (!covered) {
			Follow(&dx, &dy);
			// Moving bits sideways goes by whole bytes, 4 pixels at 2 bits.
			if (ScrollDirect(true, 4))
				dx = dx / 4 * 4;
			if (dx)
				ScrollAxis(dx, true);
			if (dy)
				ScrollAxis(dy, false);
			if (gBgMoved) {
				RectangleType all;

				all.topLeft.x = all.topLeft.y = 0;
				all.extent.x = gW;
				all.extent.y = gH;
				gBgMoved = false;
				BgToView(&all);
			}
		}
		PART(2);
	}
	if (full) {
		// The view isn't followed while the overview covers it.
		if (k->deaths != gShownDeaths || (gWasCovered && !covered))
			CentreOnTank();
		ClampView();
		DrawItemChanges(true, covered);
		PickClear();
		if (!covered) {
			BgFit();
			DrawAll();
			gBgValid = BgUse();
			gCompValid = gComposing;
		}
	}
	// Changes under the overview aren't drawn, so gBgB is stale until the
	// next full frame.
	if (covered)
		gBgValid = gCompValid = false;
	gWasCovered = covered;
	gNumOld = 0;
	PART(3);
	if (!covered)
		DrawSprites();
	if (gComposing) {
		gComposing = false;
		WinSetDrawWindow(scr);
		CompFlush(scr);
	}
	PART(4);
	gShownDeaths = k->deaths;
	MARK(LP_VIEW);
	PanelDraw(full, gH, gVx, gVy, gW, gH, gTs);
	DrawBar(full);
	MARK(LP_PANEL);
	PART(5);
	NativeOff();
#ifdef DRAW_PARTS
	if (++gPartN == 100) {
		LOG(("draw parts x100, sys ticks: erase %lu changes %lu scroll %lu full %lu sprites %lu panel %lu",
			gPart[0], gPart[1], gPart[2], gPart[3], gPart[4], gPart[5]));
		for (i = 0; i < 6; i++)
			gPart[i] = 0;
		gPartN = 0;
	}
#endif
#ifdef DEBUG
	if (full)
		LOG(("full redraw %lu ticks (%d x %d, tile %d)", TimGetTicks() - t0,
			(Int16)gW, (Int16)gH, (Int16)gTs));
	TimingDraw(t0, TimGetTicks());
#endif
}

#ifdef DRAW_BENCH
// Debug: redraws the whole frame and counts the view bytes that changed,
// which a correct partial frame leaves at 0. 0xFFFF if the view isn't
// drawn directly.
UInt16 SEC_COLD ViewCheck(void)
{
	UInt16 wb = (UInt16)(((UInt16)gW * gViewB.depth) >> 3), y, x, diff = 0;
	UInt8 *keep, *p;

	if (!gViewB.bits)
		return 0xFFFF;
	keep = MemPtrNew((UInt32)wb * gH);
	if (!keep)
		return 0xFFFF;
	for (y = 0, p = gViewB.bits; y < (UInt16)gH; y++, p += gViewB.rowBytes)
		MemMove(keep + (UInt32)y * wb, p, wb);
	ViewFrame(true, true);
	for (y = 0, p = gViewB.bits; y < (UInt16)gH; y++, p += gViewB.rowBytes)
		for (x = 0; x < wb; x++)
			if (keep[(UInt32)y * wb + x] != p[x]) {
				if (!diff)
					LOG(("view check: first difference at byte %u row %u", x, y));
				diff++;
			}
	MemPtrFree(keep);
	return diff;
}
#endif

#if defined(DEBUG) && defined(SCROLL_BENCH)
// Debug: times the parts of a scroll 50 times each, then redraws.
void SEC_COLD ViewScrollBench(void)
{
	UInt32 t[16], t0;
	UInt16 i, j, rb = gViewB.rowBytes;
	UInt32 n = (UInt32)rb * gH;
	RectangleType col, row, all, one;

	if (!gViewB.bits)
		return;
	NativeOn();
	col.topLeft.x = gW - 4;
	col.topLeft.y = 0;
	col.extent.x = 4;
	col.extent.y = gH;
	row.topLeft.x = 0;
	row.topLeft.y = gH - 4;
	row.extent.x = gW;
	row.extent.y = 4;
	all.topLeft.x = all.topLeft.y = 0;
	all.extent.x = gW;
	all.extent.y = gH;
	one.topLeft.x = 40 - ((Int16)gVx & 7);
	one.topLeft.y = 40 - ((Int16)gVy & 7);
	one.extent.x = one.extent.y = 8;
	for (j = 0; j < 16; j++) {
		gSkipBlit = j == 10 || j == 11;
		t0 = TimGetTicks();
#ifdef SCROLL_BENCH_ONE
		// For the PC sampler: only part SCROLL_BENCH_ONE, 20000 times.
		for (i = 0; i < (j == SCROLL_BENCH_ONE ? 20000U : 0); i++)
#else
		for (i = 0; i < 50; i++)
#endif
			switch (j) {
			case 10: DrawRegion(&col); break;
			case 11: DrawRegion(&row); break;
			case 12: if (gBgB.bits) BgToView(&col); break;
			case 13: if (gBgB.bits) BgToView(&row); break;
			case 14: DrawRegion(&one); break;
			case 15: DrawRegion(&all); break;
			case 0: MemMove(gViewB.bits, gViewB.bits + 1, n - 1); break;
			case 1: MemMove(gViewB.bits, gViewB.bits + 4 * rb, n - 4 * rb); break;
			case 2: BlitMove(gViewB.bits, gViewB.bits + 1, n - 1); break;
			case 3: BlitMove(gViewB.bits, gViewB.bits + 4 * rb, n - 4 * rb); break;
			case 4: BlitMove(gViewB.bits + 1, gViewB.bits, n - 1); break;
			case 5: BlitMove(gViewB.bits + 4 * rb, gViewB.bits, n - 4 * rb); break;
			case 6: DrawRegion(&col); break;
			case 7: DrawRegion(&row); break;
			case 8: if (gBgB.bits) BgToView(&all); break;
			case 9: if (gBgB.bits) BlitMove(gViewB.bits, gBgB.bits, n); break;
			}
		t[j] = TimGetTicks() - t0;
	}
	gSkipBlit = false;
	DrawAll();
	NativeOff();
	LOG(("scroll bench x50, sys ticks: MemMove odd %lu rows %lu, BlitMove odd %lu rows %lu back %lu %lu",
		t[0], t[1], t[2], t[3], t[4], t[5]));
	LOG(("scroll bench x50: strip column %lu row %lu, bg to view %lu BlitMove %lu (bg %d, %d x %d rb %u)",
		t[6], t[7], t[8], t[9], (Int16)(gBgB.bits != NULL), (Int16)gW, (Int16)gH, rb));
	LOG(("scroll bench x50: strips without blits %lu %lu, bg to view %lu %lu, one tile %lu, all %lu (view at %ld %ld)",
		t[10], t[11], t[12], t[13], t[14], t[15], gVx, gVy));
}
#endif

#ifdef DEBUG
// Draws the whole view count times and logs the average, since one redraw
// is shorter than a system tick.
void SEC_COLD ViewTimeFull(UInt16 count)
{
	UInt16 i;
	UInt32 t0, t1;

	NativeOn();
	t0 = TimGetTicks();
	for (i = 0; i < count; i++)
		DrawAll();
	t1 = TimGetTicks();
	LOG(("full redraw x%u: %lu ticks, %lu.%02lu ms each", count, t1 - t0,
		(t1 - t0) * 1000UL / gTps / count,
		((t1 - t0) * 100000UL / gTps / count) % 100));
	// Again without copying the tiles, which leaves the picking, the map
	// reads and the sprites over the tiles.
	gSkipBlit = true;
	t0 = TimGetTicks();
	for (i = 0; i < count; i++)
		DrawAll();
	t1 = TimGetTicks();
	gSkipBlit = false;
	DrawAll();
	NativeOff();
	LOG(("full redraw x%u without tile copies: %lu ticks, %lu.%02lu ms each", count, t1 - t0,
		(t1 - t0) * 1000UL / gTps / count,
		((t1 - t0) * 100000UL / gTps / count) % 100));
}
#endif
