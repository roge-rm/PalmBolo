// Tile view: draws the map with the placeholder tiles from game/tiles.c and
// scrolls it by pixels. All drawing is in the screen's native pixels, so a
// double density screen gets 16-pixel tiles and the others 8-pixel tiles.
//
// The tiles are drawn once into an offscreen window, the tile sheet, in a
// row with tile i at x = i * tileSize. To scroll, the pixels already on the
// screen are moved with WinScrollRectangle and only the strip that was
// uncovered is drawn from the sheet.
//
// The tank, shells, explosions and the gunsight are drawn over the tiles
// each frame the game ticked, and erased next time by drawing the tiles
// under them again. The view follows the tank.
#include "palmbolo.h"
#include "resource.h"
#include "game/game.h"
#include "game/mapstore.h"
#include "game/tiles.h"
#include "game/util.h"

static const GameState *gGame;
static WinHandle gSheet;
static WinHandle gTankSheet, gTankMask;  // 16 headings, one tile each
static UInt8 gShift = 5;        // world units to view pixels
// Rectangles drawn over the tiles last frame, to erase next frame.
#define kMaxRects 48
static RectangleType gOld[kMaxRects];
static UInt16 gNumOld;
static RectangleType gReadout;
static UInt8 gShown[5];
// Pills and bases as last drawn: x, y, owner, armour and inTank (pills).
static UInt8 gPillLook[MAP_MAX_PILLS][5], gBaseLook[MAP_MAX_BASES][2];
static UInt16 gShownDeaths = 0xFFFF;
static IndexedColorType gYellow, gOrange;

static void SEC_UI BuildTankSheet(void);
static void SEC_UI CentreOnTank(void);
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

// Colour indexes for the tile sheet. They are looked up while the screen is
// the draw window, since an offscreen window may not have the screen's
// palette.
static IndexedColorType gBg[TILE_COUNT], gFg[TILE_COUNT];
static IndexedColorType gWhite, gBlack, gRed;

static void SEC_UI LookupColours(void)
{
	UInt16 t;
	Boolean mono = !ScreenNewWin() || ScreenDepth() == 1;

	gWhite = ScreenIndex(255, 255, 255);
	gBlack = ScreenIndex(0, 0, 0);
	gRed = ScreenIndex(200, 0, 0);
	for (t = 0; t < TILE_COUNT; t++) {
		const TileDef *def = tile_def((t >= TERRAIN_MINE_FIRST && t <= TERRAIN_MINE_LAST) ?
			t - TILE_MINE_ADD : t);

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
static void SEC_UI DrawTile(UInt16 t, const TileDef *def, Boolean mine)
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

static void SEC_UI BuildSheet(void)
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
			DrawTile(t, tile_def(t - TILE_MINE_ADD), true);
		else
			DrawTile(t, tile_def(t), false);
	}
	WinSetDrawWindow(old);
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

// Opens or closes the input area. Returns true if it asked the system to.
Boolean SEC_UI ViewSetInputArea(Boolean closed)
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
void SEC_UI ViewResized(void)
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

void SEC_UI ViewOpen(const DeviceInfo *d, const GameState *game)
{
	gGame = game;
	gPins = d->pins;
	gHiRes = ScreenHiRes();
	gTs = gHiRes ? 16 : 8;
	gTps = d->ticksPerSecond ? d->ticksPerSecond : 100;
	gW = gH = 0;
	ViewSetInputArea(true);
	ViewResized();
	gShift = gTs == 16 ? 4 : 5;
	NativeOn();
	BuildSheet();
	gYellow = ScreenIndex(250, 220, 40);
	gOrange = ScreenIndex(230, 110, 20);
	BuildTankSheet();
	NativeOff();
	gNumOld = 0;
	gShownDeaths = 0xFFFF;
	gReadout.extent.x = gReadout.extent.y = 0;
	CentreOnTank();
}

void SEC_UI ViewClose(void)
{
	ViewSetInputArea(false);
	if (gSheet) {
		WinDeleteWindow(gSheet, false);
		gSheet = NULL;
	}
	if (gTankSheet) {
		WinDeleteWindow(gTankSheet, false);
		gTankSheet = NULL;
	}
	if (gTankMask) {
		WinDeleteWindow(gTankMask, false);
		gTankMask = NULL;
	}
}

static void SEC_UI CopyTile(UInt16 tile, Int32 tx, Int32 ty)
{
	RectangleType src;

	src.topLeft.x = tile * gTs;
	src.topLeft.y = 0;
	src.extent.x = gTs;
	src.extent.y = gTs;
	WinCopyRectangle(gSheet, WinGetDrawWindow(), &src, (Coord)(tx * gTs - gVx),
		(Coord)(ty * gTs - gVy), winPaint);
}

static UInt16 SEC_UI PillTile(const MapPill *p)
{
	UInt8 kind = game_owner_kind(gGame, p->owner, gGame->tanks[0].player);

	if (p->armour == 0)
		return TILE_PILL_DEAD;
	return kind == OWN_NEUTRAL ? TILE_PILL : TILE_PILL_OWN + kind - OWN_SELF;
}

static UInt16 SEC_UI BaseTile(const MapBase *b)
{
	UInt8 kind = game_owner_kind(gGame, b->owner, gGame->tanks[0].player);

	return kind == OWN_NEUTRAL ? TILE_BASE : TILE_BASE_OWN + kind - OWN_SELF;
}

// Draws the part of the view inside r (view pixels) from the map.
static void SEC_UI DrawRegion(const RectangleType *r)
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
		// Damage: a dark bar across the top, longer with less armour.
		if (p->armour > 0 && p->armour < R_PILL_MAX_ARMOUR)
			Fill((Coord)(p->x * gTs - gVx), (Coord)(p->y * gTs - gVy),
				(Coord)(gTs * (R_PILL_MAX_ARMOUR - p->armour) / R_PILL_MAX_ARMOUR),
				(Coord)(gTs / 4), gBlack);
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

// Tank sprites: a body and a barrel, turned to each of the 16 headings
// the tank is drawn at. The mask sheet is black where the tank is; drawn
// with winMask it clears those pixels to index 0, then the sprite sheet is
// added with winOverlay. Index 0 is white and black the last index in the
// Palm palettes, which this relies on.
static Boolean SEC_UI InTank(Int16 along, Int16 side, Boolean *barrel)
{
	Int16 ts = gTs * 2;     // the arguments are in half pixels

	if (side < 0)
		side = -side;
	*barrel = along >= 0 && along <= ts / 2 && side <= (ts >= 32 ? 3 : 1);
	if (*barrel)
		return true;
	return along >= -ts * 2 / 5 && along <= ts / 4 && side <= ts * 3 / 10;
}

static void SEC_UI BuildTankSheet(void)
{
	UInt16 err, d;
	WinHandle old;
	Coord px, py;
	IndexedColorType body = ScreenIndex(235, 235, 235), gun = ScreenIndex(0, 0, 0);
	Boolean mono = !ScreenNewWin() || ScreenDepth() == 1, barrel;
	UInt16 fmt = gHiRes ? nativeFormat : screenFormat;

	if (mono)
		body = gun;
	if (!gTankSheet)
		gTankSheet = WinCreateOffscreenWindow(16 * gTs, gTs, fmt, &err);
	if (!gTankMask)
		gTankMask = WinCreateOffscreenWindow(16 * gTs, gTs, fmt, &err);
	if (!gTankSheet || !gTankMask)
		return;
	old = WinSetDrawWindow(gTankSheet);
	Fill(0, 0, 16 * gTs, gTs, 0);
	WinSetDrawWindow(gTankMask);
	Fill(0, 0, 16 * gTs, gTs, 0);
	for (d = 0; d < 16; d++) {
		int16_t fx, fy;

		util_distance(&fx, &fy, (uint16_t)(d * 16 * ANGLE_ONE), 64);
		for (py = 0; py < gTs; py++) {
			for (px = 0; px < gTs; px++) {
				Int16 dx = px * 2 + 1 - gTs, dy = py * 2 + 1 - gTs;
				Int16 along = (Int16)((dx * fx + dy * fy) / 64);
				Int16 side = (Int16)((dy * fx - dx * fy) / 64);

				if (!InTank(along, side, &barrel))
					continue;
				WinSetDrawWindow(gTankMask);
				Fill(d * gTs + px, py, 1, 1, gun);
				WinSetDrawWindow(gTankSheet);
				Fill(d * gTs + px, py, 1, 1, barrel ? gun : body);
			}
		}
	}
	WinSetDrawWindow(old);
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

static void SEC_UI DrawTank(const Tank *k)
{
	RectangleType src;
	Coord x = WorldX(k->x) - gTs / 2, y = WorldY(k->y) - gTs / 2;

	if (!gTankSheet || k->destroyed)
		return;
	src.topLeft.x = util_dir16(k->angle) * gTs;
	src.topLeft.y = 0;
	src.extent.x = gTs;
	src.extent.y = gTs;
	WinCopyRectangle(gTankMask, WinGetDrawWindow(), &src, x, y, winMask);
	WinCopyRectangle(gTankSheet, WinGetDrawWindow(), &src, x, y, winOverlay);
	Remember(x, y, gTs, gTs);
}

static void SEC_UI DrawSprites(void)
{
	const GameState *g = gGame;
	const Tank *k = &g->tanks[0];
	UInt16 i;
	Coord x, y, r, dot = gTs / 8 + 1;

	for (i = 0; i < g->numExpl; i++) {
		r = (Coord)(gTs * g->expl[i].frame / 16);
		if (r < 1)
			r = 1;
		x = WorldX(g->expl[i].x);
		y = WorldY(g->expl[i].y);
		Fill(x - r, y - r, r * 2, r * 2, (g->expl[i].frame & 2) ? gYellow : gOrange);
		Remember(x - r, y - r, r * 2, r * 2);
	}
	DrawTank(k);
	for (i = 0; i < g->numShells; i++) {
		x = WorldX(g->shells[i].x) - dot / 2;
		y = WorldY(g->shells[i].y) - dot / 2;
		Fill(x, y, dot, dot, gBlack);
		Remember(x, y, dot, dot);
	}
	if (!k->destroyed && k->showSight) {
		uint16_t wx, wy;

		game_gunsight(g, 0, &wx, &wy);
		x = WorldX((uint16_t)(wx + 128));
		y = WorldY((uint16_t)(wy + 128));
		r = gTs / 2;
		Fill(x - r, y, r * 2, 1, gRed);
		Fill(x, y - r, 1, r * 2, gRed);
		Remember(x - r, y - r, r * 2 + 1, r * 2 + 1);
	}
}

// Armour, shells, mines and trees, top left. Redrawn when they change or
// something was drawn over them.
static void SEC_UI DrawReadout(Boolean force)
{
	const Tank *k = &gGame->tanks[0];
	Char buf[48], *p = buf;
	UInt8 v[5];
	UInt16 i;
	FontID old;

	v[0] = k->armour;
	v[1] = k->shells;
	v[2] = k->mines;
	v[3] = k->trees;
	v[4] = k->numCarry;
	if (!force && MemCmp(v, gShown, 5) == 0 && gShownDeaths == k->deaths)
		return;
	MemMove(gShown, v, 5);
	gShownDeaths = k->deaths;
	for (i = 0; i < 5; i++) {
		*p++ = "ASMTP"[i];
		StrIToA(p, v[i]);
		p += StrLen(p);
		*p++ = ' ';
	}
	*--p = 0;
	old = FntSetFont(stdFont);
	gReadout.topLeft.x = 1;
	gReadout.topLeft.y = 1;
	gReadout.extent.x = FntCharsWidth(buf, StrLen(buf)) + 2;
	gReadout.extent.y = FntLineHeight();
	WinEraseRectangle(&gReadout, 0);
	WinDrawChars(buf, StrLen(buf), 2, 1);
	FntSetFont(old);
}

static Boolean SEC_UI Overlaps(const RectangleType *a, const RectangleType *b)
{
	return a->topLeft.x < b->topLeft.x + b->extent.x && b->topLeft.x < a->topLeft.x + a->extent.x &&
		a->topLeft.y < b->topLeft.y + b->extent.y && b->topLeft.y < a->topLeft.y + a->extent.y;
}

// Redraws the tiles of squares whose terrain changed.
static void SEC_UI DrawChanges(void)
{
	uint8_t xy[64];
	uint8_t n = game_take_changes(xy, 32), i;
	RectangleType r;

	if (n == 255) {
		DrawAll();
		return;
	}
	for (i = 0; i < n; i++) {
		r.topLeft.x = (Coord)((Int32)xy[i * 2] * gTs - gVx);
		r.topLeft.y = (Coord)((Int32)xy[i * 2 + 1] * gTs - gVy);
		r.extent.x = gTs;
		r.extent.y = gTs;
		if (r.topLeft.x > -gTs && r.topLeft.x < gW && r.topLeft.y > -gTs && r.topLeft.y < gH)
			DrawRegion(&r);
	}
}

static void SEC_UI Erase(const RectangleType *r);

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
static void SEC_UI DrawItemChanges(Boolean all)
{
	const MapInfo *m = &gGame->map;
	UInt16 i;

	for (i = 0; i < m->numPills; i++) {
		const MapPill *p = &m->pills[i];
		UInt8 *o = gPillLook[i];

		if (o[0] == p->x && o[1] == p->y && o[2] == p->owner && o[3] == p->armour &&
		    o[4] == p->inTank)
			continue;
		if (!all) {
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
		if (!all)
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
	const Tank *k = &gGame->tanks[0];
	uint16_t sx, sy;
	Int32 tx, ty, mx = gW / 4, my = gH / 4;

	game_gunsight(gGame, 0, &sx, &sy);
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
	gVx = ((Int32)gGame->tanks[0].x >> gShift) - gW / 2;
	gVy = ((Int32)gGame->tanks[0].y >> gShift) - gH / 2;
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
	Boolean readout = full;
	const Tank *k = &gGame->tanks[0];
#ifdef DEBUG
	UInt32 t0 = TimGetTicks();
#endif

	if (!full && !ticked)
		return;
	NativeOn();
	if (!full) {
		for (i = 0; i < gNumOld; i++) {
			if (Overlaps(&gOld[i], &gReadout))
				readout = true;
			Erase(&gOld[i]);
		}
		DrawChanges();
		DrawItemChanges(false);
		if (k->deaths != gShownDeaths) {
			full = true;
		} else {
			Follow(&dx, &dy);
			if (dx || dy) {
				Erase(&gReadout);
				if (dx)
					ScrollAxis(dx, true);
				if (dy)
					ScrollAxis(dy, false);
				readout = true;
			}
		}
	}
	if (full) {
		if (k->deaths != gShownDeaths)
			CentreOnTank();
		ClampView();
		DrawItemChanges(true);
		DrawAll();
		readout = true;
	}
	gNumOld = 0;
	DrawSprites();
	DrawReadout(readout);
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
