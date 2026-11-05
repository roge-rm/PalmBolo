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
// The tanks, builder, shells, explosions and the gunsight are drawn over
// the tiles each frame the game ticked, and erased next time by drawing the
// tiles under them again. The view follows the tank.
//
// Under the view is the status panel (panel.c), then along the bottom a
// row of buttons: the five builder actions, the one chosen shown inverted,
// and the gunsight nearer and further.
#include "palmbolo.h"
#include "resource.h"
#include "game/game.h"
#include "game/mapstore.h"
#include "game/util.h"
#include "tilepick.h"
#include "art.h"

static const GameState *gGame;
static WinHandle gSheet, gSprites, gMasks;
static UInt8 gShift = 5;        // world units to view pixels
// Rectangles drawn over the tiles last frame, to erase next frame: room
// for every explosion, tank, builder and shell and the gunsight.
#define kMaxRects (GAME_MAX_EXPL + 2 * GAME_MAX_TANKS + GAME_MAX_SHELLS + 1)
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
static void SEC_UI CellRect(UInt16 cell, RectangleType *r);
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
static IndexedColorType gWhite, gBlack, gRed;

static void SEC_UI LookupColours(void)
{
	gWhite = ScreenIndex(255, 255, 255);
	gBlack = ScreenIndex(0, 0, 0);
	gRed = ScreenIndex(200, 0, 0);
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
static WinHandle SEC_UI NewWindow1(Coord w, Coord ht)
{
	UInt16 err = 0;
	WinHandle win = NULL;

	gMaskBmp = BmpCreate(w, ht, 1, NULL, &err);
	if (gMaskBmp && gHiRes) {
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
static WinHandle SEC_UI LoadSheet(UInt16 id, UInt16 cells, Boolean oneBit)
{
	UInt16 err = 0;
	WinHandle win, old;
	MemHandle h;
	Coord w = ART_COLS * gTs, ht = (Coord)((cells + ART_COLS - 1) / ART_COLS) * gTs;

	if (oneBit)
		win = NewWindow1(w, ht);
	else
		win = WinCreateOffscreenWindow(w, ht, gHiRes ? nativeFormat : screenFormat, &err);
	h = DmGetResource(bitmapRsc, id);
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

static void SEC_UI FreeSheets(void)
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
}

// Copies mask cell 0 from the 1-bit sheet with winMask onto a cell filled
// with AllOnes, as DrawSprite does, and checks a set mask pixel came out 0
// and a clear one stayed. Says whether this OS converts a 1-bit source by
// the fore and back colours.
static Boolean SEC_UI Mask1Works(void)
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
static Boolean SEC_UI LoadSheets(void)
{
	FreeSheets();
	LookupColours();
	gSheet = LoadSheet(ArtTerrainBmp, ART_TERRAIN_CELLS, false);
	gSprites = LoadSheet(ArtSpriteBmp, ART_SPRITE_CELLS, false);
	// The mask is 1-bit where the OS can blit that onto the screen's depth,
	// else in the screen's format.
	if (ScreenNewWin() && ScreenDepth() > 1) {
		gMasks = LoadSheet(ArtMaskBmp, ART_MASK_CELLS, true);
		gMask1 = gMasks != NULL;
		if (gMask1 && !Mask1Works()) {
			LOG(("1-bit mask doesn't blit here, using a full depth one"));
			FreeMask1(gMasks);
			gMasks = NULL;
			gMask1 = false;
		}
	}
	if (!gMasks)
		gMasks = LoadSheet(ArtMaskBmp, ART_MASK_CELLS, false);
	LOG(("mask sheet %s", gMask1 ? "1-bit" : "screen depth"));
	return gSheet && gSprites && gMasks;
}

// Native pixel drawing is on for as long as the view draws.
static void SEC_UI NativeOn(void)
{
	if (gHiRes)
		gOldCoord = WinSetCoordinateSystem(kCoordinatesNative);
}

static void SEC_UI NativeOff(void)
{
	if (gHiRes)
		WinSetCoordinateSystem(gOldCoord);
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
void SEC_UI ViewResized(void)
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

Boolean SEC_UI ViewOpen(const DeviceInfo *d, const GameState *game)
{
	Boolean ok;

	// Masking needs an indexed screen; a 16-bit one is refused.
	if (ScreenDepth() > 8) {
		LOG(("screen depth %u: not drawn", ScreenDepth()));
		FrmCustomAlert(ViewAlert, "PalmBolo can't draw on a 16-bit screen.", "", "");
		return false;
	}
	gGame = game;
	gPins = d->pins;
	gHiRes = ScreenHiRes();
	gTs = gHiRes ? 16 : 8;
	gTps = d->ticksPerSecond ? d->ticksPerSecond : 100;
	gW = gH = 0;
	ViewFormInputArea(true);
	ViewResized();
	gShift = gTs == 16 ? 4 : 5;
	NativeOn();
	ok = LoadSheets();
	PanelOpen(d, game);
	NativeOff();
	if (!ok) {
		FreeSheets();
		FrmCustomAlert(ViewAlert, "Not enough memory for the art.", "", "");
		return false;
	}
	gNumOld = 0;
	gShownDeaths = 0xFFFF;
	gWasCovered = false;
	CentreOnTank();
	return true;
}

void SEC_UI ViewClose(void)
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

static void SEC_UI CopyTile(UInt16 cell, Int32 tx, Int32 ty)
{
	RectangleType src;

	CellRect(cell, &src);
	WinCopyRectangle(gSheet, WinGetDrawWindow(), &src, (Coord)(tx * gTs - gVx),
		(Coord)(ty * gTs - gVy), winPaint);
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
static void SEC_UI BasesAsRoad(Int32 tx, Int32 ty, uint8_t n[8])
{
	const MapInfo *m = &gGame->map;
	UInt16 i;

	for (i = 0; i < m->numBases; i++) {
		Int16 dx = (Int8)(UInt8)(m->bases[i].x - tx), dy = (Int8)(UInt8)(m->bases[i].y - ty);

		if (!m->bases[i].active || dx < -1 || dx > 1 || dy < -1 || dy > 1 || (dx == 0 && dy == 0))
			continue;
		n[dy < 0 ? TP_A + dx : dy > 0 ? TP_B + dx : dx < 0 ? TP_L : TP_R] = TERRAIN_ROAD;
	}
}

// Draws the part of the view inside r (view pixels) from the map.
static void SEC_UI DrawRegion(const RectangleType *r)
{
	Int32 tx0 = (gVx + r->topLeft.x) / gTs, tx1 = (gVx + r->topLeft.x + r->extent.x - 1) / gTs;
	Int32 ty0 = (gVy + r->topLeft.y) / gTs, ty1 = (gVy + r->topLeft.y + r->extent.y - 1) / gTs;
	Int32 tx, ty;
	UInt16 i;
	const MapInfo *m = &gGame->map;
	const uint8_t *up, *row, *down;
	uint8_t n[8];

	if (!gSheet || r->extent.x <= 0 || r->extent.y <= 0)
		return;
	if (tx1 > MAP_SIZE - 1)
		tx1 = MAP_SIZE - 1;
	if (ty1 > MAP_SIZE - 1)
		ty1 = MAP_SIZE - 1;
	WinSetClip(r);
	for (ty = ty0; ty <= ty1; ty++) {
		up = mapstore_row((uint8_t)(ty - 1));
		row = mapstore_row((uint8_t)ty);
		down = mapstore_row((uint8_t)(ty + 1));
		for (tx = tx0; tx <= tx1; tx++) {
			uint8_t t = row[tx];
			Boolean mine = false;

			// A hidden mine the player wasn't shown is drawn without it.
			if (t >= TERRAIN_MINE_FIRST && t <= TERRAIN_MINE_LAST) {
				mine = !gGame->hiddenMines || game_mine_shown(gGame, (uint8_t)tx, (uint8_t)ty);
				t = (uint8_t)(t - TERRAIN_MINE_ADD);
			}
			if (tile_has_variants(t)) {
				tile_neighbours(up, row, down, (uint8_t)tx, n);
				if (tile_bases_matter(t))
					BasesAsRoad(tx, ty, n);
			}
			CopyTile(tile_pick(t, n), tx, ty);
			if (mine)
				DrawSprite(AS_MINE, (Coord)(tx * gTs - gVx), (Coord)(ty * gTs - gVy));
		}
	}
	for (i = 0; i < m->numStarts; i++)
		if (m->starts[i].x >= tx0 && m->starts[i].x <= tx1 &&
		    m->starts[i].y >= ty0 && m->starts[i].y <= ty1)
			CopyTile(AT_START, m->starts[i].x, m->starts[i].y);
	for (i = 0; i < m->numBases; i++) {
		const MapBase *b = &m->bases[i];

		if (b->active && b->x >= tx0 && b->x <= tx1 && b->y >= ty0 && b->y <= ty1)
			CopyTile(BaseTile(b), b->x, b->y);
	}
	for (i = 0; i < m->numPills; i++) {
		const MapPill *p = &m->pills[i];

		if (!p->active || p->inTank || p->x < tx0 || p->x > tx1 || p->y < ty0 || p->y > ty1)
			continue;
		CopyTile(PillTile(p), p->x, p->y);
	}
	WinResetClip();
}

static void SEC_UI DrawAll(void)
{
	RectangleType r;

	r.topLeft.x = r.topLeft.y = 0;
	r.extent.x = gW;
	r.extent.y = gH;
	DrawRegion(&r);
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
	if (horizontal)
		gVx = now;
	else
		gVy = now;
	if (dist >= (horizontal ? gW : gH)) {
		DrawAll();
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
// erasing.
static void SEC_UI DrawAt(UInt16 cell, uint16_t wx, uint16_t wy)
{
	Coord x = WorldX(wx) - gTs / 2, y = WorldY(wy) - gTs / 2;

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
	Coord x, y, r;
	RectangleType clip;

	clip.topLeft.x = clip.topLeft.y = 0;
	clip.extent.x = gW;
	clip.extent.y = gH;
	WinSetClip(&clip);

	for (i = 0; i < g->numExpl; i++) {
		Int16 f = R_EXPLOSION_START - g->expl[i].frame;

		DrawAt(AS_EXPL_0 + (f < 0 ? 0 : f > 7 ? 7 : f), g->expl[i].x, g->expl[i].y);
	}
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
		x = WorldX((uint16_t)(wx + 128));
		y = WorldY((uint16_t)(wy + 128));
		r = gTs / 2;
		Fill(x - r, y, r * 2, 1, gRed);
		Fill(x, y - r, 1, r * 2, gRed);
		Remember(x - r, y - r, r * 2 + 1, r * 2 + 1);
	}
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
static void SEC_UI DrawBar(Boolean force)
{
	const Char *p = kButtonText;
	RectangleType r;
	UInt16 i;
	Coord x0, x1, len;
	FontID old;
	IndexedColorType oldBack = 0;

	if (!force && gBarShown == gAction)
		return;
	gBarShown = gAction;
	old = FntSetFont(stdFont);
	if (ScreenNewWin())
		oldBack = WinSetBackColor(PanelBg());
	Fill(0, gBarY, gW, gBarH, PanelBg());
	Fill(0, gBarY, gW, 1, gBlack);
	Fill(0, gBarY + 1, gW, 1, gWhite);
	for (i = 0; i < kButtons; i++, p += len + 1) {
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
		PanelRebuild();
		if (!covered)
			DrawAll();
		return;
	}
	for (i = 0; i < n; i++)
		PanelSquare(xy[i * 2], xy[i * 2 + 1]);
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

// Draws the tiles back over r, clipped to the view.
static void SEC_UI Erase(const RectangleType *r)
{
	RectangleType c = *r;

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
	DrawRegion(&c);
}

void SEC_UI ViewFrame(Boolean full, Boolean ticked)
{
	Int32 dx, dy;
	UInt16 i;
	Boolean covered = PanelCovers();
	const Tank *k = &gGame->tanks[game_viewer_tank(gGame)];
#ifdef DEBUG
	UInt32 t0 = TimGetTicks();
#endif

	if (!full && !ticked)
		return;
	NativeOn();
	if (!full) {
		if (!covered)
			for (i = 0; i < gNumOld; i++)
				Erase(&gOld[i]);
		DrawChanges(covered);
		DrawItemChanges(false, covered);
		if (k->deaths != gShownDeaths) {
			full = true;
		} else if (!covered) {
			Follow(&dx, &dy);
			if (dx)
				ScrollAxis(dx, true);
			if (dy)
				ScrollAxis(dy, false);
		}
	}
	if (full) {
		// The view isn't followed while the overview covers it.
		if (k->deaths != gShownDeaths || (gWasCovered && !covered))
			CentreOnTank();
		ClampView();
		DrawItemChanges(true, covered);
		if (!covered)
			DrawAll();
	}
	gWasCovered = covered;
	gNumOld = 0;
	if (!covered)
		DrawSprites();
	gShownDeaths = k->deaths;
	PanelDraw(full, gH, gVx, gVy, gW, gH, gTs);
	DrawBar(full);
	NativeOff();
#ifdef DEBUG
	if (full)
		LOG(("full redraw %lu ticks (%d x %d, tile %d)", TimGetTicks() - t0,
			(Int16)gW, (Int16)gH, (Int16)gTs));
	TimingDraw(t0, TimGetTicks());
#endif
}

#ifdef DEBUG
// Draws the whole view count times and logs the average, since one redraw
// is shorter than a system tick.
void SEC_UI ViewTimeFull(UInt16 count)
{
	UInt16 i;
	UInt32 t0, t1;

	NativeOn();
	t0 = TimGetTicks();
	for (i = 0; i < count; i++)
		DrawAll();
	t1 = TimGetTicks();
	NativeOff();
	LOG(("full redraw x%u: %lu ticks, %lu.%02lu ms each", count, t1 - t0,
		(t1 - t0) * 1000UL / gTps / count,
		((t1 - t0) * 100000UL / gTps / count) % 100));
}
#endif
