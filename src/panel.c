// Status panel and overview map.
//
// The panel shows armour, shells, mines and trees as small bars with the
// number, one mark for each pillbox and base shaped by owner (and shaded
// by owner on colour screens), and what the builder is doing. It only
// redraws what changed.
//
// The overview map is the whole map at one pixel to two by two squares,
// drawn once into an offscreen window (128 x 128 pixels: 2 KB at 1 bit up
// to 16 KB at 8 bits) and patched when a square or a pill or base changes.
// The tank and the view rectangle are drawn over it and put back from the
// window when they move.
//
// The panel is a two line strip under the view, and the overview covers
// the view when the strip is tapped.
//
// All drawing is in native pixels, so the caller has native coordinates on
// on a double density screen.
#include "palmbolo.h"
#include "screendraw.h"
#include "game/game.h"
#include "game/mapstore.h"
#include "netgame.h"
#include "art.h"

#define kOv         128         // overview window size in pixels

enum { C_LAND, C_DEEP, C_WATER, C_FOREST, C_ROAD, C_BUILD, C_COUNT };

static const GameState *gGame;
static WinHandle gOv;
static Boolean gHiRes, gNewWin, gMono, gColour;
static Boolean gCovers;
static Coord gW, gPanelH, gU = 1, gTs = 8;
static Coord gMs = 1;       // pill and base mark: a 3 by 3 grid of gMs pixel cells
static IndexedColorType gWhite, gBlack, gBg;     // gBg: panel and button bar ground
static IndexedColorType gCls[C_COUNT];      // overview terrain colours
static IndexedColorType gOwn[4];            // neutral, own, ally, enemy
static IndexedColorType gGauge[4];
static UInt16 gFore = 0xFFFF;               // last foreground colour set, 0xFFFF unknown
static UInt8 gShownV[4], gShownMan, gShownCarry;
static UInt32 gBlink[4];                  // tick the icon's flash ends, 0 none
static IndexedColorType gAmber, gEmpty;
static UInt8 gPillSig[MAP_MAX_PILLS], gBaseSig[MAP_MAX_BASES];
static UInt8 gShownPills, gShownBases;
static UInt32 gShownSec = 0xFFFFFFFFUL;    // time left drawn, in seconds
static Boolean gStatusDrawn;
static UInt32 gScanTick;                   // game tick >> 4 of the last scan of the marks
// What the overview showed on screen: the tank's pixel, the view rectangle
// and a dirty rectangle of window pixels to copy again.
static Coord gTankX = -1, gTankY, gViewX, gViewY, gViewW, gViewH;
static Coord gDx0, gDy0, gDx1, gDy1;       // dirty, x1 and y1 exclusive
static Boolean gOvShown;

static void SEC_UI Fill(Coord x, Coord y, Coord w, Coord h, IndexedColorType c)
{
	RectangleType r;

	if (w <= 0 || h <= 0)
		return;
	r.topLeft.x = x;
	r.topLeft.y = y;
	r.extent.x = w;
	r.extent.y = h;
	if (gNewWin) {
		WinSetForeColor(c);
		gFore = c;
		WinDrawRectangle(&r, 0);
	} else if (c) {
		WinDrawRectangle(&r, 0);
	} else {
		WinEraseRectangle(&r, 0);
	}
}

static void SEC_UI Erase(Coord x, Coord y, Coord w, Coord h)
{
	Fill(x, y, w, h, gBg);
}

static void SEC_UI SetBlack(void)
{
	if (gNewWin) {
		WinSetForeColor(gBlack);
		gFore = gBlack;
	}
}

// Colour screens: dark land, light forest, teal water, deep blue,
// grey roads, brown buildings. Grey screens: lighter to darker.
static const UInt8 col[C_COUNT][3] = {
	{0, 51, 0}, {0, 51, 153}, {0, 153, 153}, {51, 153, 51},
	{170, 170, 170}, {153, 102, 51}};
static const UInt8 grey[C_COUNT] = {255, 68, 119, 187, 34, 0};

// Colour for terrain class c on a screen with colour, grey or one bit.
static void SEC_COLD LookupColours(Boolean hasColor)
{
	UInt16 i;

	gWhite = ScreenIndex(255, 255, 255);
	gBlack = ScreenIndex(0, 0, 0);
	gColour = hasColor && ScreenDepth() >= 8;
	gMono = !gNewWin || ScreenDepth() == 1;
	gBg = gColour ? ScreenIndex(204, 204, 204) : gWhite;
	for (i = 0; i < C_COUNT; i++)
		gCls[i] = gColour ? ScreenIndex(col[i][0], col[i][1], col[i][2]) :
			ScreenIndex(grey[i], grey[i], grey[i]);
	gOwn[OWN_NEUTRAL] = ScreenIndex(102, 102, 102);
	gOwn[OWN_SELF] = ScreenIndex(0, 204, 0);
	gOwn[OWN_ALLY] = ScreenIndex(0, 153, 255);
	gOwn[OWN_ENEMY] = ScreenIndex(255, 0, 0);
	gAmber = ScreenIndex(255, 153, 0);
	gEmpty = ScreenIndex(255, 0, 0);
	gGauge[0] = ScreenIndex(0, 204, 0);         // armour
	gGauge[1] = ScreenIndex(255, 204, 0);       // shells
	gGauge[2] = ScreenIndex(255, 102, 0);       // mines
	gGauge[3] = ScreenIndex(51, 153, 51);       // trees
	if (!gColour) {
		gAmber = gEmpty = gBlack;
		for (i = 0; i < 4; i++) {
			gOwn[i] = gBlack;
			gGauge[i] = gBlack;
		}
	}
}

// ---- overview window ----

static UInt8 SEC_UI ClassOf(UInt8 t)
{
	if (t >= TERRAIN_MINE_FIRST && t <= TERRAIN_MINE_LAST)
		t = (UInt8)(t - TERRAIN_MINE_ADD);
	switch (t) {
	case TERRAIN_BUILDING:
	case TERRAIN_HALFBUILDING:
		return C_BUILD;
	case TERRAIN_ROAD:
		return C_ROAD;
	case TERRAIN_FOREST:
		return C_FOREST;
	case TERRAIN_RIVER:
	case TERRAIN_BOAT:
		return C_WATER;
	case TERRAIN_DEEP_SEA:
		return C_DEEP;
	}
	return C_LAND;
}

// The pixel for class c at window pixel x, y. One bit screens use
// patterns: forest sparse, deep sea checkered, water and built-up black.
static IndexedColorType SEC_UI PixelIndex(UInt8 c, Coord x, Coord y)
{
	Boolean on;

	if (!gMono)
		return gCls[c];
	switch (c) {
	case C_DEEP:
		on = (x + y) & 1;
		break;
	case C_FOREST:
		on = ((x + y) & 3) == 0;
		break;
	case C_LAND:
		on = false;
		break;
	default:
		on = true;
	}
	return on ? gBlack : gWhite;
}

static void SEC_UI Plot(Coord x, Coord y, IndexedColorType c)
{
	if (gNewWin) {
		if (c != gFore) {
			WinSetForeColor(c);
			gFore = c;
		}
		WinDrawPixel(x, y);
	} else if (c) {
		// WinDrawPixel and WinErasePixel are OS 3.5.
		WinDrawLine(x, y, x, y);
	} else {
		WinEraseLine(x, y, x, y);
	}
}

// Class of the two by two squares at window pixel px, py: the one that
// shows most.
static UInt8 gClassOf[256];     // ClassOf for every terrain byte, made by PanelOpen

static UInt8 SEC_UI BlockClass(Coord px, Coord py)
{
	const uint8_t *r0 = mapstore_row((uint8_t)(py * 2)) + px * 2;
	const uint8_t *r1 = mapstore_row((uint8_t)(py * 2 + 1)) + px * 2;
	UInt8 best = gClassOf[r0[0]], c;

	c = gClassOf[r0[1]];
	if (c > best)
		best = c;
	c = gClassOf[r1[0]];
	if (c > best)
		best = c;
	c = gClassOf[r1[1]];
	if (c > best)
		best = c;
	return best;
}

// Mark shapes on a 3 by 3 grid, bit 0 the top left: solid, ring, cross,
// dot, bar, plus.
#define kShapeSolid 0x1FF
#define kShapeRing  0x1EF
#define kShapeCross 0x145
#define kShapeDot   0x010
#define kShapeBar   0x038
#define kShapePlus  0x0BA

static UInt16 SEC_UI ShapeOf(UInt8 kind)
{
	return kind == OWN_SELF ? kShapeSolid : kind == OWN_ALLY ? kShapeRing :
		kind == OWN_ENEMY ? kShapeCross : kShapeDot;
}

// Draws a mark with its top left at x, y, each grid cell s pixels square.
static void SEC_UI DrawShape(Coord x, Coord y, Coord s, UInt16 shape, IndexedColorType c)
{
	UInt16 i;

	for (i = 0; i < 9; i++)
		if (shape & (1U << i))
			Fill(x + (Coord)(i % 3) * s, y + (Coord)(i / 3) * s, s, s, c);
}

// An item's mark in the overview window, with a one pixel halo so it
// shows on any ground: pills two pixels square, bases three.
static void SEC_UI OvMark(UInt8 x, UInt8 y, UInt8 owner, Boolean base, Boolean dead)
{
	UInt8 kind = game_owner_kind(gGame, owner, game_viewer());
	Coord px = x >> 1, py = y >> 1, n = base ? 3 : 2, i, j;
	IndexedColorType c = gOwn[kind];

	px -= base ? 1 : 0;
	py -= base ? 1 : 0;
	Fill(px - 1, py - 1, n + 2, n + 2, gColour ? gBlack : gWhite);
	if (gColour) {
		if (dead)
			Fill(px, py, n, 1, c);
		else
			Fill(px, py, n, n, c);
		return;
	}
	if (base) {
		UInt16 s = ShapeOf(kind);

		for (i = 0; i < 9; i++)
			if (s & (1U << i))
				Plot(px + i % 3, py + i / 3, gBlack);
		return;
	}
	// Pills: own four pixels, allied three, enemy two, neutral and dead one.
	for (j = 0; j < 2; j++)
		for (i = 0; i < 2; i++) {
			Boolean on = kind == OWN_SELF || (kind == OWN_ALLY && (i || j)) ||
				(kind == OWN_ENEMY && i == j) || i + j == 0;

			if (dead && i + j)
				on = false;
			if (on)
				Plot(px + i, py + j, gBlack);
		}
}

static void SEC_UI OvItems(void)
{
	const MapInfo *m = &gGame->map;
	UInt16 i;

	for (i = 0; i < m->numPills; i++)
		if (m->pills[i].active && !m->pills[i].inTank)
			OvMark(m->pills[i].x, m->pills[i].y, m->pills[i].owner, false,
				m->pills[i].armour == 0);
	for (i = 0; i < m->numBases; i++)
		if (m->bases[i].active)
			OvMark(m->bases[i].x, m->bases[i].y, m->bases[i].owner, true, false);
}

static void SEC_UI Dirty(Coord x0, Coord y0, Coord x1, Coord y1)
{
	if (x0 < 0)
		x0 = 0;
	if (y0 < 0)
		y0 = 0;
	if (x1 > kOv)
		x1 = kOv;
	if (y1 > kOv)
		y1 = kOv;
	if (x1 <= x0 || y1 <= y0)
		return;
	if (gDx1 <= gDx0) {
		gDx0 = x0;
		gDy0 = y0;
		gDx1 = x1;
		gDy1 = y1;
		return;
	}
	if (x0 < gDx0)
		gDx0 = x0;
	if (y0 < gDy0)
		gDy0 = y0;
	if (x1 > gDx1)
		gDx1 = x1;
	if (y1 > gDy1)
		gDy1 = y1;
}

static BlitWin gOvB;        // the window's pixels, where they can be written directly

// Writes window pixels x0..x1-1 of row y from the map straight into the
// window, as Plot would draw them.
static void SEC_UI OvRowDirect(Coord y, Coord x0, Coord x1)
{
	UInt8 *row = gOvB.bits + (UInt32)y * gOvB.rowBytes;
	UInt16 d = gOvB.depth, ppb = 8 / d;
	UInt8 ones = (UInt8)((1 << d) - 1);
	Coord x;

	for (x = x0; x < x1; x++) {
		UInt16 sh = (UInt16)((ppb - 1 - (x & (ppb - 1))) * d);   // first pixel in the top bits
		UInt8 *p = row + x / ppb;

		*p = (UInt8)((*p & ~(ones << sh)) |
			((PixelIndex(BlockClass(x, y), x, y) ? ones : 0) << sh));
		if (!(x & 7))
			NetService();
	}
}

// Redraws the whole window from the map.
static void SEC_UI OvBuild(void)
{
	WinHandle old;
	Coord x, y, run;
	UInt8 cls[kOv];

	if (!gOv)
		return;
	if (gOvB.bits) {
		for (y = 0; y < kOv; y++)
			OvRowDirect(y, 0, kOv);
		old = WinSetDrawWindow(gOv);
		OvItems();
		WinSetDrawWindow(old);
		gFore = 0xFFFF;
		Dirty(0, 0, kOv, kOv);
		return;
	}
	old = WinSetDrawWindow(gOv);
	gFore = 0xFFFF;
	for (y = 0; y < kOv; y++) {
		for (x = 0; x < kOv; x++) {
			cls[x] = BlockClass(x, y);
			if (!(x & 7))
				NetService();
		}
		for (x = 0; x < kOv; x += run) {
			IndexedColorType c = PixelIndex(cls[x], x, y);

			// A run of the same colour is one line.
			for (run = 1; x + run < kOv && cls[x + run] == cls[x] &&
			     PixelIndex(cls[x], x + run, y) == c; run++)
				;
			if (gNewWin) {
				if (c != gFore) {
					WinSetForeColor(c);
					gFore = c;
				}
				WinDrawLine(x, y, x + run - 1, y);
			} else if (c) {
				WinDrawLine(x, y, x + run - 1, y);
			} else {
				WinEraseLine(x, y, x + run - 1, y);
			}
			NetService();
		}
	}
	OvItems();
	WinSetDrawWindow(old);
	gFore = 0xFFFF;
	Dirty(0, 0, kOv, kOv);
}

// Redraws the window pixels x0..x0+w-1, y0..y0+h-1 from the map and puts
// the marks back over them.
static void SEC_UI OvPatch(Coord x0, Coord y0, Coord w, Coord h)
{
	WinHandle old;
	RectangleType r;
	Coord x, y;

	if (x0 < 0) {
		w += x0;
		x0 = 0;
	}
	if (y0 < 0) {
		h += y0;
		y0 = 0;
	}
	if (x0 + w > kOv)
		w = kOv - x0;
	if (y0 + h > kOv)
		h = kOv - y0;
	if (!gOv || w <= 0 || h <= 0)
		return;
	r.topLeft.x = x0;
	r.topLeft.y = y0;
	r.extent.x = w;
	r.extent.y = h;
	ScreenAlignRect(&r);
	x0 = r.topLeft.x;
	y0 = r.topLeft.y;
	w = r.extent.x;
	h = r.extent.y;
	old = WinSetDrawWindow(gOv);
	gFore = 0xFFFF;
	for (y = y0; y < y0 + h; y++) {
		if (gOvB.bits) {
			OvRowDirect(y, x0, x0 + w);
			continue;
		}
		for (x = x0; x < x0 + w; x++)
			Plot(x, y, PixelIndex(BlockClass(x, y), x, y));
	}
	r.topLeft.x = x0;
	r.topLeft.y = y0;
	r.extent.x = w;
	r.extent.y = h;
	WinSetClip(&r);
	OvItems();
	WinResetClip();
	WinSetDrawWindow(old);
	gFore = 0xFFFF;
	Dirty(x0, y0, x0 + w, y0 + h);
}

void SEC_UI PanelSquare(UInt8 x, UInt8 y)
{
	OvPatch(x >> 1, y >> 1, 1, 1);
}

void SEC_UI PanelItem(UInt8 x, UInt8 y)
{
	OvPatch((x >> 1) - 2, (y >> 1) - 2, 5, 5);
}

void SEC_UI PanelRebuild(void)
{
	OvBuild();
}

// ---- layout ----

Coord SEC_COLD PanelLayout(Coord w)
{
	gW = w;
	gHiRes = ScreenHiRes();
	gU = gHiRes ? 2 : 1;
	gTs = gHiRes ? 16 : 8;
	gMs = gHiRes ? 3 : 1;
	// Two lines on top, the icon row, then the marks with a pixel round
	// them (and one spare under them at 160 by 160).
	gPanelH = gTs + 3 * gMs + (gHiRes ? 5 : 6);
	gStatusDrawn = false;
	gOvShown = false;
	return gPanelH;
}

Boolean SEC_UI PanelCovers(void)
{
	return gCovers;
}

// Shows or hides the overview over the view. True if that changed
// anything.
Boolean SEC_UI PanelToggle(void)
{
	if (!gOv)
		return false;
	gCovers = !gCovers;
	gStatusDrawn = false;
	gOvShown = false;
	return true;
}

void SEC_COLD PanelOpen(const DeviceInfo *d, const GameState *game)
{
	UInt16 err;
	UInt16 i;

	gGame = game;
	for (i = 0; i < 256; i++)
		gClassOf[i] = ClassOf((UInt8)i);
	gHiRes = ScreenHiRes();
	gNewWin = ScreenNewWin();
	gU = gHiRes ? 2 : 1;
	gTs = gHiRes ? 16 : 8;
	LookupColours(d->hasColor);
	gCovers = false;
	gStatusDrawn = false;
	gOvShown = false;
	gTankX = -1;
	if (gOv)
		WinDeleteWindow(gOv, false);
	gOv = WinCreateOffscreenWindow(kOv, kOv, gHiRes ? nativeFormat : screenFormat, &err);
	gOvB.bits = NULL;
	if (gOv)
		BlitFind(gOv, &gOvB);
#ifdef DEBUG
	if (gOv && ScreenOs40()) {
		BitmapType *bmp = WinGetBitmap(gOv);
		Coord bw = 0, bh = 0;
		UInt16 rb = 0;

		BmpGetDimensions(bmp, &bw, &bh, &rb);
		LOG(("overview %d x %d depth %u, %lu bytes", (Int16)bw, (Int16)bh,
			BmpGetBitDepth(bmp), (UInt32)rb * bh));
	} else if (!gOv) {
		LOG(("overview: no window (err %x)", err));
	}
#endif
	OvBuild();
}

void SEC_COLD PanelClose(void)
{
	if (gOv)
		WinDeleteWindow(gOv, false);
	gOv = NULL;
	gOvB.bits = NULL;
}

// ---- status ----

// Left edge of slot i of n across the width, so slots share it evenly
// the way the button bar does.
static Coord SEC_UI SlotX(UInt16 i, UInt16 n)
{
	return (Coord)((Int32)gW * i / n);
}

// Icon, bar and (on hi-res) number for gauge i in the box x, y, w wide.
// The bar is the colour of the stock, amber under a quarter and an empty
// red frame at none; screens without colour show those as solid, dotted
// and an empty frame. inv draws the icon inverted, the blink at none.
static void SEC_UI DrawGauge(UInt8 i, Coord x, Coord y, Coord w, Boolean inv)
{
	UInt16 v = gShownV[i];
	Coord isz = gTs, nw = 0, bx = x + isz + 2 * gU, bh = 4 * gU + 1, by = y + (isz - bh) / 2 + 1;
	Coord bw, fw, k;
	IndexedColorType c = v == 0 ? gEmpty : v < R_TANK_FULL_ARMOUR / 4 ? gAmber : gGauge[i];

	Erase(x, y, w, isz + 1);
	ViewIcon(i == 0 ? AS_ICON_ARMOUR : i == 1 ? AS_ICON_SHELL : i == 2 ? AS_ICON_MINE : AS_ICON_TREE,
		x, y + 1);
	if (inv) {
		RectangleType r;

		r.topLeft.x = x;
		r.topLeft.y = y + 1;
		r.extent.x = r.extent.y = isz;
		WinInvertRectangle(&r, 0);
	}
	if (gHiRes) {
		Char buf[6];

		StrIToA(buf, v);
		nw = FntCharsWidth("40", 2) + 2 * gU;
		SetBlack();
		WinDrawChars(buf, StrLen(buf), x + w - nw + gU, y + (isz - FntLineHeight()) / 2 + 1);
	}
	bw = w - (bx - x) - nw;
	if (bw < 6)
		return;
	Fill(bx, by, bw, bh, v == 0 ? c : gBlack);
	Fill(bx + 1, by + 1, bw - 2, bh - 2, gWhite);
	fw = (Coord)((Int32)(bw - 2) * (v > R_TANK_FULL_ARMOUR ? R_TANK_FULL_ARMOUR : v) / R_TANK_FULL_ARMOUR);
	if (gColour || v >= R_TANK_FULL_ARMOUR / 4) {
		Fill(bx + 1, by + 1, fw, bh - 2, c);
	} else {
		for (k = 0; k < fw; k += 2)
			Fill(bx + 1 + k, by + 1, 1, bh - 2, gBlack);
	}
}

static UInt8 SEC_UI PillKind(const MapPill *p)
{
	return game_owner_kind(gGame, p->owner, game_viewer());
}

// One mark for each pill or base in a row rowH high at y. The row is cut
// into a slot for every possible pill and then every possible base, and
// each mark sits in the middle of its slot. A pill that is dead or a base
// that can't give any is shown as a bar, a pill the tank carries as a plus.
static void SEC_UI DrawMarks(Boolean base, Coord y, Coord rowH)
{
	const MapInfo *m = &gGame->map;
	UInt16 i, n = base ? m->numBases : m->numPills, first = base ? MAP_MAX_PILLS : 0;
	UInt16 slots = MAP_MAX_PILLS + MAP_MAX_BASES;
	Coord s = gMs, my = y + (rowH - 3 * s) / 2, x0 = SlotX(first, slots);

	Erase(x0, y, SlotX(first + MAP_MAX_PILLS, slots) - x0, rowH);
	for (i = 0; i < n; i++) {
		UInt8 kind;
		UInt16 shape;
		Boolean dead;

		if (base) {
			kind = game_owner_kind(gGame, m->bases[i].owner, game_viewer());
			dead = m->bases[i].armour <= R_BASE_CAPTURE_ARMOUR;
		} else {
			kind = PillKind(&m->pills[i]);
			dead = m->pills[i].armour == 0;
		}
		shape = dead ? kShapeBar : ShapeOf(kind);
		if (!base && m->pills[i].inTank)
			shape = kShapePlus;
		x0 = SlotX(first + i, slots);
		x0 += (SlotX(first + i + 1, slots) - x0 - 3 * s) / 2;
		// On hi-res the neutral dot is a 5 pixel square, a single cell
		// is too small to see.
		if (shape == kShapeDot && gHiRes)
			Fill(x0 + s - 1, my + s - 1, s + 2, s + 2, gOwn[kind]);
		else
			DrawShape(x0, my, s, shape, gOwn[kind]);
	}
}

// 3 by 5 digits, a row to a byte (bit 2 is the left pixel), then the colon.
static const UInt8 kDigit[11][5] = {
	{7, 5, 5, 5, 7}, {2, 6, 2, 2, 7}, {7, 1, 7, 4, 7}, {7, 1, 7, 1, 7},
	{5, 5, 7, 1, 1}, {7, 4, 7, 1, 7}, {7, 4, 7, 5, 7}, {7, 1, 1, 1, 1},
	{7, 5, 7, 5, 7}, {7, 5, 7, 1, 7}, {0, 2, 0, 2, 0}};

// The time left in a timed game as m:ss at the right end of the marks row,
// in digits drawn from rectangles so they fit the row's height.
static void SEC_UI DrawTimeLeft(Coord y, Coord rowH, UInt32 sec)
{
	Char t[8];
	Coord sc = gHiRes ? 2 : 1, x, w, ty;
	UInt16 n, i, r, c;

	StrPrintF(t, "%lu:%02lu", sec / 60, sec % 60);
	n = StrLen(t);
	w = (Coord)(n * 4 * sc - sc);
	x = gW - w - 2 * gU;
	ty = y + (rowH - 5 * sc) / 2;
	Erase(x - sc, y, w + 2 * sc, rowH);
	for (i = 0; i < n; i++) {
		UInt8 d = t[i] == ':' ? 10 : (UInt8)(t[i] - '0');

		for (r = 0; r < 5; r++)
			for (c = 0; c < 3; c++)
				if (kDigit[d][r] & (4 >> c))
					Fill(x + (Coord)((i * 4 + c) * sc), ty + (Coord)(r * sc), sc, sc, gBlack);
	}
}

// The builder's icon and, when the tank carries pills, a pill icon and how
// many, centred in the box x, y, w wide.
static void SEC_UI DrawBuilder(Coord x, Coord y, Coord w, UInt8 man, UInt8 carry)
{
	Coord isz = gTs, cw = isz, nw = 0;
	Char buf[4];

	Erase(x, y, w, isz + 1);
	if (carry) {
		StrIToA(buf, carry);
		nw = FntCharsWidth(buf, StrLen(buf));
		if (cw + 3 * gU + isz + nw <= w)
			cw += 3 * gU + isz + nw;
		else
			carry = 0;
	}
	x += (w - cw) / 2;
	ViewIcon(man == 0 ? AS_ICON_MANIN : man == 1 ? AS_ICON_MANOUT : man == 2 ? AS_ICON_MANBACK :
		AS_ICON_PARA, x, y + 1);
	if (carry) {
		x += isz + 2 * gU;
		ViewIcon(AS_ICON_PILL, x, y + 1);
		SetBlack();
		WinDrawChars(buf, StrLen(buf), x + isz + gU, y + (isz - FntLineHeight()) / 2 + 1);
	}
}

// What a pill or base mark shows, as a byte per mark to compare with the
// last time. kt gives the owner's kind for the owners 0..GAME_MAX_PLAYERS
// (the last is neutral). The loops use pointers and no calls, as indexing
// a struct array costs a multiply call on the 68000.
static void SEC_UI MakeKinds(UInt8 *kt, UInt8 viewer)
{
	UInt8 o;

	for (o = 0; o < GAME_MAX_PLAYERS; o++) {
		const Player *p = &gGame->players[o];

		kt[o] = o == viewer ? OWN_SELF :
			viewer < GAME_MAX_PLAYERS && p->inUse && ((p->allies >> viewer) & 1) ? OWN_ALLY : OWN_ENEMY;
	}
	kt[GAME_MAX_PLAYERS] = OWN_NEUTRAL;
}

static Boolean SEC_UI ScanMarks(UInt8 viewer, Boolean *pc, Boolean *bc)
{
	const MapInfo *m = &gGame->map;
	const MapPill *pp = m->pills;
	const MapBase *bp = m->bases;
	UInt8 *sp = gPillSig, kt[GAME_MAX_PLAYERS + 1], v, o;
	UInt16 i;

	MakeKinds(kt, viewer);
	for (i = m->numPills; i; i--, pp++, sp++) {
		o = pp->owner;
		v = (UInt8)(kt[o < GAME_MAX_PLAYERS ? o : GAME_MAX_PLAYERS] | (pp->armour == 0 ? 4 : 0) |
			(pp->inTank ? 8 : 0));
		if (v != *sp)
			*pc = true;
		*sp = v;
	}
	sp = gBaseSig;
	for (i = m->numBases; i; i--, bp++, sp++) {
		o = bp->owner;
		v = (UInt8)(kt[o < GAME_MAX_PLAYERS ? o : GAME_MAX_PLAYERS] |
			(bp->armour <= R_BASE_CAPTURE_ARMOUR ? 4 : 0) | (bp->active ? 0 : 8));
		if (v != *sp)
			*bc = true;
		*sp = v;
	}
	return true;
}

// Whether gauge i has to be drawn: its value changed or its blink ended.
// The clock is only read when a blink starts or is running.
static Boolean SEC_UI NeedGauge(const Tank *k, UInt8 i, Boolean force)
{
	UInt8 v = i == 0 ? k->armour : i == 1 ? k->shells : i == 2 ? k->mines : k->trees;
	Boolean changed = force || v != gShownV[i], ended = false;

	// Running out makes the icon flash once.
	if (v == 0 && gShownV[i] != 0 && !force)
		gBlink[i] = TimGetTicks() + SysTicksPerSecond() / 3;
	if (gBlink[i] && TimGetTicks() >= gBlink[i]) {
		gBlink[i] = 0;
		ended = true;
	}
	gShownV[i] = v;
	return changed || ended;
}

static void SEC_UI DrawStatus(Coord py)
{
	const MapInfo *m = &gGame->map;
	Boolean force = !gStatusDrawn, pc = false, bc = false, tc = false, gc[4], mc;
	UInt16 i;
	UInt8 viewer = game_viewer(), man, carry, ti = 0;
	const Tank *k;
	const Builder *b;
	FontID old;
	Coord y, isz = gTs, rowM;
	UInt32 left = 0;

	// What changed is worked out first without any drawing calls, so a
	// frame where nothing did costs next to nothing.
	for (k = gGame->tanks; ti < gGame->numTanks; ti++, k++)
		if (k->player == viewer)
			break;
	if (ti >= gGame->numTanks) {
		ti = 0;
		k = gGame->tanks;
	}
	b = &gGame->men[ti];
	man = b->isDead ? 3 : b->inTank ? 0 : b->state == LGM_STATE_GOING ? 1 : 2;
	carry = k->numCarry;
	if (force)
		gShownPills = gShownBases = 0xFF;
	// Walking every mark costs about a millisecond on the Palm IIIx, so it
	// is done on a full redraw and every 16 ticks (about 3 times a second).
	if (force || (gGame->tick >> 4) != gScanTick) {
		gScanTick = gGame->tick >> 4;
		ScanMarks(viewer, &pc, &bc);
	}
	if (gShownPills != m->numPills) {
		pc = true;
		gShownPills = (UInt8)m->numPills;
	}
	if (gShownBases != m->numBases) {
		bc = true;
		gShownBases = (UInt8)m->numBases;
	}
	for (i = 0; i < 4; i++)
		gc[i] = NeedGauge(k, (UInt8)i, force);
	mc = force || man != gShownMan || carry != gShownCarry;
	if (gGame->timeLimit) {
		left = gGame->tick >= gGame->timeLimit ? 0 :
			(gGame->timeLimit - gGame->tick + GAME_TICKS_PER_SECOND - 1) / GAME_TICKS_PER_SECOND;
		tc = force || bc || left != gShownSec;
		gShownSec = left;
	}
	gShownMan = man;
	gShownCarry = carry;
	if (!force && !pc && !bc && !tc && !mc && !gc[0] && !gc[1] && !gc[2] && !gc[3])
		return;

	old = FntSetFont(stdFont);
	if (force) {
		Erase(0, py, gW, gPanelH);
		Fill(0, py, gW, 1, gBlack);
		Fill(0, py + 1, gW, 1, gWhite);
	}
	rowM = 3 * gMs + 4;
	// The four gauges and the builder share the width in five even slots,
	// with a gap of gU on each side of a slot.
	y = py + 2;
	for (i = 0; i < 4; i++)
		if (gc[i])
			DrawGauge((UInt8)i, SlotX(i, 5) + gU, y, SlotX(i + 1, 5) - SlotX(i, 5) - 2 * gU,
				gBlink[i] != 0);
	if (mc)
		DrawBuilder(SlotX(4, 5) + gU, y, gW - SlotX(4, 5) - 2 * gU, man, carry);
	y += isz + 1;
	if (force || pc)
		DrawMarks(false, y, rowM - 2);
	if (force || bc)
		DrawMarks(true, y, rowM - 2);
	if (tc)
		DrawTimeLeft(y, rowM - 2, left);
	gStatusDrawn = true;
	FntSetFont(old);
}

// ---- overview on screen ----

static void SEC_UI Invert(Coord x, Coord y, Coord w, Coord h)
{
	RectangleType r;

	r.topLeft.x = x;
	r.topLeft.y = y;
	r.extent.x = w;
	r.extent.y = h;
	WinInvertRectangle(&r, 0);
}

// Copies window pixels x0..x1-1, y0..y1-1 to the screen at ox, oy.
static void SEC_UI Blit(Coord ox, Coord oy, Coord x0, Coord y0, Coord x1, Coord y1)
{
	RectangleType r;

	if (x0 < 0)
		x0 = 0;
	if (y0 < 0)
		y0 = 0;
	if (x1 > kOv)
		x1 = kOv;
	if (y1 > kOv)
		y1 = kOv;
	if (x1 <= x0 || y1 <= y0)
		return;
	r.topLeft.x = x0;
	r.topLeft.y = y0;
	r.extent.x = x1 - x0;
	r.extent.y = y1 - y0;
	WinCopyRectangle(gOv, WinGetDrawWindow(), &r, ox + x0, oy + y0, winPaint);
}

// The overview at ox, oy with the tank and the view rectangle over it.
// vx, vy, vw, vh are the view in world pixels at ts pixels a square.
static void SEC_UI DrawOverview(Coord ox, Coord oy, Boolean force, Int32 vx, Int32 vy,
	Coord vw, Coord vh, Coord ts)
{
	const Tank *k = &gGame->tanks[game_viewer_tank(gGame)];
	Coord tx = (Coord)(k->x >> 9), ty = (Coord)(k->y >> 9);
	Coord sq = ts * 2, x0 = (Coord)(vx / sq), y0 = (Coord)(vy / sq);
	Coord w = (Coord)((vx + vw - 1) / sq) - x0 + 1, h = (Coord)((vy + vh - 1) / sq) - y0 + 1;
	Boolean moved = tx != gTankX || ty != gTankY || x0 != gViewX || y0 != gViewY ||
		w != gViewW || h != gViewH;

	if (!gOv)
		return;
	if (!force && !moved && gDx1 <= gDx0)
		return;
	if (force || !gOvShown) {
		Blit(ox, oy, 0, 0, kOv, kOv);
	} else {
		// Put back what the tank, the rectangle and patches covered.
		Blit(ox, oy, gViewX - 1, gViewY - 1, gViewX + gViewW + 1, gViewY + gViewH + 1);
		Blit(ox, oy, gTankX - 3, gTankY - 3, gTankX + 4, gTankY + 4);
		Blit(ox, oy, gDx0, gDy0, gDx1, gDy1);
	}
	gDx1 = gDx0 = 0;
	gOvShown = true;
	gTankX = tx;
	gTankY = ty;
	gViewX = x0;
	gViewY = y0;
	gViewW = w;
	gViewH = h;
	if (x0 + w > kOv)
		w = kOv - x0;
	if (y0 + h > kOv)
		h = kOv - y0;
	Invert(ox + x0, oy + y0, w, 1);
	if (h > 1)
		Invert(ox + x0, oy + y0 + h - 1, w, 1);
	if (h > 2) {
		Invert(ox + x0, oy + y0 + 1, 1, h - 2);
		if (w > 1)
			Invert(ox + x0 + w - 1, oy + y0 + 1, 1, h - 2);
	}
	Fill(ox + tx - 2, oy + ty - 2, 5, 5, gBlack);
	Fill(ox + tx - 1, oy + ty - 1, 3, 3, gWhite);
}

IndexedColorType SEC_UI PanelBg(void)
{
	return gBg;
}

static void SEC_UI DrawIn(Boolean force, Coord py, Int32 vx, Int32 vy, Coord vw, Coord vh, Coord ts)
{
	if (gCovers) {
		Coord bottom = py + gPanelH;

		if (force || !gOvShown)
			Erase(0, 0, gW, bottom);
		DrawOverview((gW - kOv) / 2, (bottom - kOv) / 2, force, vx, vy, vw, vh, ts);
		return;
	}
	if (force)
		gStatusDrawn = false;
	DrawStatus(py);
}

// Draws what changed. py is the top of the panel, just under the view.
void SEC_UI PanelDraw(Boolean force, Coord py, Int32 vx, Int32 vy, Coord vw, Coord vh, Coord ts)
{
	IndexedColorType oldBack = 0;

	if (gNewWin)
		oldBack = WinSetBackColor(gBg);
	DrawIn(force, py, vx, vy, vw, vh, ts);
	if (gNewWin)
		WinSetBackColor(oldBack);
}
