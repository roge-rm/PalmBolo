// Tile view: draws the map with the placeholder tiles from game/tiles.c and
// scrolls it by pixels. All drawing is in the screen's native pixels, so a
// double density screen gets 16-pixel tiles and the others 8-pixel tiles.
//
// The tiles are drawn once into an offscreen window, the tile sheet, in a
// row with tile i at x = i * tileSize. To scroll, the pixels already on the
// screen are moved with WinScrollRectangle and only the strip that was
// uncovered is drawn from the sheet.
#include "palmbolo.h"
#include "resource.h"
#include "game/game.h"
#include "game/mapstore.h"
#include "game/tiles.h"

#define kKeyNavLeft     0x01000000UL
#define kKeyNavRight    0x02000000UL
#define kKeyNavSelect   0x04000000UL

// Scroll speed in pixels a second at 8-pixel tiles. Doubled at 16.
#define kScrollSpeed    88

static const GameState *gGame;
static WinHandle gSheet;
static Coord gTs = 8;           // tile size in pixels
static Coord gW, gH;            // view size
static Int32 gVx, gVy;          // world pixel at the top left of the view
static UInt16 gTps = 100;
static UInt32 gLastScroll;
static UInt32 gAcc;             // scroll distance not yet a whole pixel, in 1/gTps pixels
static Boolean gInputClosed;
static Boolean gPins;
static Boolean gHiRes;
static UInt16 gOldCoord;

static void Fill(Coord x, Coord y, Coord w, Coord h, IndexedColorType c)
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

// Colour indexes for the tile sheet. They are looked up while the screen is
// the draw window, since an offscreen window may not have the screen's
// palette.
static IndexedColorType gBg[TILE_COUNT], gFg[TILE_COUNT];
static IndexedColorType gWhite, gBlack, gRed;

static void LookupColours(void)
{
	UInt16 t;
	Boolean mono = !ScreenNewWin() || ScreenDepth() == 1;

	gWhite = ScreenIndex(255, 255, 255);
	gBlack = ScreenIndex(0, 0, 0);
	gRed = ScreenIndex(200, 0, 0);
	for (t = 0; t < TILE_COUNT; t++) {
		const TileDef *def = &kTiles[(t >= TERRAIN_MINE_FIRST && t <= TERRAIN_MINE_LAST) ?
			t - TILE_MINE_ADD : t];

		if (mono) {
			gBg[t] = tile_mono(def->bg) ? gBlack : gWhite;
			gFg[t] = tile_mono(def->bg) ? gWhite : gBlack;
		} else {
			gBg[t] = ScreenIndex(def->bg[0], def->bg[1], def->bg[2]);
			gFg[t] = ScreenIndex(def->fg[0], def->fg[1], def->fg[2]);
			// On a grey screen two colours can land on the same grey.
			if (gBg[t] == gFg[t])
				gFg[t] = tile_mono(def->bg) ? gWhite : gBlack;
		}
	}
}

// Draws tile t of the sheet from design def, with a mine mark if asked.
static void DrawTile(UInt16 t, const TileDef *def, Boolean mine)
{
	Coord s = gTs / 8, x0 = t * gTs, px, py, run;
	Boolean mono = !ScreenNewWin() || ScreenDepth() == 1;

	Fill(x0, 0, gTs, gTs, gBg[t]);
	for (py = 0; py < 8; py++) {
		for (px = 0; px < 8; px++) {
			if (!((def->pat[py] >> (7 - px)) & 1))
				continue;
			for (run = 1; px + run < 8 && ((def->pat[py] >> (7 - px - run)) & 1); run++)
				;
			Fill(x0 + px * s, py * s, run * s, s, gFg[t]);
			px += run - 1;
		}
	}
	if (mine) {
		Fill(x0 + 2 * s, 2 * s, 4 * s, 4 * s, gWhite);
		Fill(x0 + 3 * s, 3 * s, 2 * s, 2 * s, mono ? gBlack : gRed);
	}
}

static void BuildSheet(void)
{
	UInt16 err, t;
	WinHandle old;

	if (gSheet)
		WinDeleteWindow(gSheet, false);
	gSheet = WinCreateOffscreenWindow(TILE_COUNT * gTs, gTs,
		gHiRes ? nativeFormat : screenFormat, &err);
#ifdef DEBUG
	if (gSheet) {
		BitmapType *bmp = WinGetBitmap(gSheet);
		UInt16 sd = 0;
		Coord bw = 0, bh = 0;

		if (bmp) {
			sd = BmpGetBitDepth(bmp);
			BmpGetDimensions(bmp, &bw, &bh, NULL);
		}
		LOG(("tile sheet %lx err %x want %d x %d got depth %u %d x %d dens %u screen depth %u",
			(UInt32)gSheet, err, (Int16)(TILE_COUNT * gTs), (Int16)gTs, sd,
			(Int16)bw, (Int16)bh, bmp ? BmpGetDensity(bmp) : 0, ScreenDepth()));
	}
#endif
	if (!gSheet)
		return;
	LookupColours();
	old = WinSetDrawWindow(gSheet);
	for (t = 0; t < TILE_COUNT; t++) {
		if (t >= TERRAIN_MINE_FIRST && t <= TERRAIN_MINE_LAST)
			DrawTile(t, &kTiles[t - TILE_MINE_ADD], true);
		else
			DrawTile(t, &kTiles[t], false);
	}
	WinSetDrawWindow(old);
}

// Native pixel drawing is on for as long as the view draws.
static void NativeOn(void)
{
	if (gHiRes)
		gOldCoord = WinSetCoordinateSystem(kCoordinatesNative);
}

static void NativeOff(void)
{
	if (gHiRes)
		WinSetCoordinateSystem(gOldCoord);
}

static Boolean PinsAvailable(void)
{
	UInt32 v;

	return gPins && FtrGet(pinCreator, pinFtrAPIVersion, &v) == errNone &&
		v >= pinAPIVersion1_0;
}

// Opens or closes the input area. Returns true if it asked the system to.
Boolean ViewSetInputArea(Boolean closed)
{
	FormType *frm = FrmGetActiveForm();

	if (!PinsAvailable() || !frm || closed == gInputClosed)
		return false;
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
void ViewResized(void)
{
	FormType *frm = FrmGetActiveForm();
	RectangleType r;
	Coord w, h;
	Int32 cx = gVx + gW / 2, cy = gVy + gH / 2;

	if (frm) {
		WinGetBounds(WinGetDisplayWindow(), &r);
		WinSetBounds(FrmGetWindowHandle(frm), &r);
	}
	NativeOn();
	WinGetDisplayExtent(&w, &h);
	NativeOff();
	gW = w;
	gH = h;
	gVx = cx - gW / 2;
	gVy = cy - gH / 2;
}

static void ClampView(void)
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

void ViewOpen(const DeviceInfo *d, const GameState *game)
{
	Int32 cx = 128 * 8, cy = 128 * 8;

	gGame = game;
	gPins = d->pins;
	gHiRes = ScreenHiRes();
	gTs = gHiRes ? 16 : 8;
	gTps = d->ticksPerSecond ? d->ticksPerSecond : 100;
	gLastScroll = TimGetTicks();
	gW = gH = 0;
	ViewSetInputArea(true);
	ViewResized();
	NativeOn();
	BuildSheet();
	NativeOff();
	if (game->map.numStarts) {
		cx = (Int32)game->map.starts[0].x * 8 + 4;
		cy = (Int32)game->map.starts[0].y * 8 + 4;
	}
	gVx = cx * (gTs / 8) - gW / 2;
	gVy = cy * (gTs / 8) - gH / 2;
	ClampView();
}

void ViewClose(void)
{
	ViewSetInputArea(false);
	if (gSheet) {
		WinDeleteWindow(gSheet, false);
		gSheet = NULL;
	}
}

static void CopyTile(UInt16 tile, Int32 tx, Int32 ty)
{
	RectangleType src;

	src.topLeft.x = tile * gTs;
	src.topLeft.y = 0;
	src.extent.x = gTs;
	src.extent.y = gTs;
	WinCopyRectangle(gSheet, WinGetDrawWindow(), &src, (Coord)(tx * gTs - gVx),
		(Coord)(ty * gTs - gVy), winPaint);
}

// Draws the part of the view inside r (view pixels) from the map.
static void DrawRegion(const RectangleType *r)
{
	Int32 tx0 = (gVx + r->topLeft.x) / gTs, tx1 = (gVx + r->topLeft.x + r->extent.x - 1) / gTs;
	Int32 ty0 = (gVy + r->topLeft.y) / gTs, ty1 = (gVy + r->topLeft.y + r->extent.y - 1) / gTs;
	Int32 tx, ty;
	UInt16 i;
	const MapInfo *m = &gGame->map;
	const uint8_t *row;

	if (!gSheet || r->extent.x <= 0 || r->extent.y <= 0)
		return;
	if (tx1 > MAP_SIZE - 1)
		tx1 = MAP_SIZE - 1;
	if (ty1 > MAP_SIZE - 1)
		ty1 = MAP_SIZE - 1;
	WinSetClip(r);
	for (ty = ty0; ty <= ty1; ty++) {
		row = mapstore_row((uint8_t)ty);
		for (tx = tx0; tx <= tx1; tx++)
			CopyTile(tile_for_terrain(row[tx]) , tx, ty);
	}
	for (i = 0; i < m->numStarts; i++)
		if (m->starts[i].x >= tx0 && m->starts[i].x <= tx1 &&
		    m->starts[i].y >= ty0 && m->starts[i].y <= ty1)
			CopyTile(TILE_START, m->starts[i].x, m->starts[i].y);
	for (i = 0; i < m->numBases; i++)
		if (m->bases[i].x >= tx0 && m->bases[i].x <= tx1 &&
		    m->bases[i].y >= ty0 && m->bases[i].y <= ty1)
			CopyTile(TILE_BASE, m->bases[i].x, m->bases[i].y);
	for (i = 0; i < m->numPills; i++)
		if (m->pills[i].x >= tx0 && m->pills[i].x <= tx1 &&
		    m->pills[i].y >= ty0 && m->pills[i].y <= ty1)
			CopyTile(TILE_PILL, m->pills[i].x, m->pills[i].y);
	WinResetClip();
}

static void DrawAll(void)
{
	RectangleType r;

	r.topLeft.x = r.topLeft.y = 0;
	r.extent.x = gW;
	r.extent.y = gH;
	DrawRegion(&r);
}

// Moves the view by dx along one axis (horizontal) or dy, redrawing the
// strip that was uncovered.
static void ScrollAxis(Int32 d, Boolean horizontal)
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

static void Held(Int16 *dx, Int16 *dy)
{
	UInt32 k = KeyCurrentState();

	*dx = *dy = 0;
	if (k & (keyBitHard1 | kKeyNavLeft))
		*dx -= 1;
	if (k & (keyBitHard4 | kKeyNavRight))
		*dx += 1;
	if (k & (keyBitHard2 | keyBitPageUp))
		*dy -= 1;
	if (k & (keyBitHard3 | keyBitPageDown))
		*dy += 1;
}

#ifdef DEBUG
static UInt32 gFrames, gFrameTicks, gFramePx, gFirstFrame, gLastFrame;

// Logs the scroll frames since the last call, once the keys are let go.
static void LogScroll(void)
{
	UInt32 draw = gFrames ? gFrameTicks * 100000UL / gTps / gFrames : 0;
	UInt32 span = gFrames ? (gLastFrame - gFirstFrame) * 100000UL / gTps / gFrames : 0;

	if (!gFrames)
		return;
	LOG(("scroll %lu frames, %lu px, draw %lu ticks (%lu.%02lu ms a frame), one frame every %lu.%02lu ms",
		gFrames, gFramePx, gFrameTicks, draw / 100, draw % 100, span / 100, span % 100));
	gFrames = gFrameTicks = gFramePx = 0;
}
#endif

void ViewFrame(Boolean full)
{
	Int16 dx, dy;
	UInt32 now = TimGetTicks();
	Int32 px = 0;
#ifdef DEBUG
	UInt32 t0 = now;
#endif

	if (!full) {
		Held(&dx, &dy);
		if (!dx && !dy) {
			gLastScroll = now;
			gAcc = 0;
#ifdef DEBUG
			LogScroll();
#endif
			return;
		}
		// Distance for the time since the last move; a shorter time waits.
		gAcc += (now - gLastScroll) * kScrollSpeed * (gTs / 8);
		gLastScroll = now;
		px = (Int32)(gAcc / gTps);
		gAcc %= gTps;
		if (px == 0)
			return;
		if (px > gW / 2)
			px = gW / 2;
	}
	NativeOn();
	if (full) {
		ClampView();
		DrawAll();
	} else {
		if (dx)
			ScrollAxis(dx * px, true);
		if (dy)
			ScrollAxis(dy * px, false);
	}
	NativeOff();
#ifdef DEBUG
	if (full)
		LOG(("full redraw %lu ticks (%d x %d, tile %d)", TimGetTicks() - t0,
			(Int16)gW, (Int16)gH, (Int16)gTs));
	else {
		if (!gFrames)
			gFirstFrame = t0;
		gLastFrame = t0;
		gFrames++;
		gFrameTicks += TimGetTicks() - t0;
		gFramePx += px;
		TimingDraw(t0, TimGetTicks());
	}
#endif
}

#ifdef DEBUG
// Draws the whole view count times and logs the average, since one redraw
// is shorter than a system tick.
void ViewTimeFull(UInt16 count)
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
