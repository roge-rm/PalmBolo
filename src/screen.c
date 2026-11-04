// Screen: depth and coordinate system setup, and the test screen. The game
// area is drawn straight to the window, no form controls.
#include "palmbolo.h"

static UInt32 gOldDepth;
static Boolean gDepthSet;
static Boolean gHiRes;          // drawing in the native (double density) system
static Boolean gNewWin;         // OS 3.5 or later: colour and screen mode calls
static UInt16 gDepth = 1;
static IndexedColorType gDark, gLight, gHot, gText, gBack;

static IndexedColorType RgbIndex(UInt8 r, UInt8 g, UInt8 b)
{
	RGBColorType c;

	c.index = 0;
	c.r = r;
	c.g = g;
	c.b = b;
	return WinRGBToIndex(&c);
}

void ScreenInit(const DeviceInfo *d)
{
	UInt32 depth;
	UInt16 i;

	gNewWin = d->rom >= kRom35;
	gHiRes = d->winVersion >= 4 && d->density == kDensityDouble;
	gDepth = 1;

	if (gNewWin) {
		// Deepest supported depth up to 8 bits.
		depth = 1;
		for (i = 8; i >= 1; i--) {
			if (d->depthMask & (1UL << (i - 1))) {
				depth = i;
				break;
			}
		}
		if (WinScreenMode(winScreenModeGet, NULL, NULL, &gOldDepth, NULL) == errNone) {
			if (WinScreenMode(winScreenModeSet, NULL, NULL, &depth, NULL) == errNone) {
				gDepthSet = true;
				gDepth = depth;
			} else {
				gDepth = gOldDepth;
			}
		}
		gDark = RgbIndex(0x20, 0x40, 0x90);
		gLight = RgbIndex(0x60, 0xA0, 0xE0);
		gHot = RgbIndex(0xFF, 0xD0, 0x20);
		gText = RgbIndex(0, 0, 0);
		gBack = RgbIndex(0xFF, 0xFF, 0xFF);
	}
}

void ScreenExit(void)
{
	if (gDepthSet) {
		UInt32 depth = gOldDepth;

		WinScreenMode(winScreenModeSet, NULL, NULL, &depth, NULL);
		gDepthSet = false;
	}
}

static void Rect(Coord x, Coord y, Coord w, Coord h, IndexedColorType c, Boolean dark)
{
	RectangleType r;

	r.topLeft.x = x;
	r.topLeft.y = y;
	r.extent.x = w;
	r.extent.y = h;
	if (gNewWin) {
		WinSetForeColor(c);
		WinDrawRectangle(&r, 0);
	} else if (dark) {
		WinDrawRectangle(&r, 0);
	} else {
		WinEraseRectangle(&r, 0);
	}
}

// Draws one test tile. The tile at index hot is lit.
static void Tile(Coord tx, Coord ty, Coord ts, Coord cols, UInt32 hot)
{
	Boolean odd = (tx + ty) & 1;

	Rect(tx * ts, ty * ts, ts - 1, ts - 1,
		(UInt32)(ty * cols + tx) == hot ? gHot : (odd ? gLight : gDark), odd);
}

static void Line(const Char *s, Coord x, Coord *y)
{
	WinDrawChars(s, StrLen(s), x, *y);
	*y += FntLineHeight();
}

void HexStr(UInt32 v, Char *out)
{
	static const Char digits[] = "0123456789ABCDEF";
	Int16 i;

	for (i = 7; i >= 0; i--) {
		out[i] = digits[v & 15];
		v >>= 4;
	}
	out[8] = 0;
}

void ScreenDraw(const DeviceInfo *d, UInt32 tick, UInt32 checksum, Boolean full)
{
	Coord w, h, ts, tx, ty, cols, rows, y, boxH;
	UInt32 hot;
	static UInt32 prevHot;
	UInt16 oldCoord = 0;
	Char s[48], hex[9];

	if (gHiRes)
		oldCoord = WinSetCoordinateSystem(kCoordinatesNative);
	WinGetDisplayExtent(&w, &h);
	ts = gHiRes ? 16 : 8;
	cols = w / ts;
	rows = h / ts;
	hot = (tick / 10) % (cols * rows);

	if (gNewWin) {
		WinSetBackColor(gBack);
		WinSetTextColor(gText);
	}
	if (full) {
		Rect(0, 0, w, h, gDark, true);
		for (ty = 0; ty < rows; ty++)
			for (tx = 0; tx < cols; tx++)
				Tile(tx, ty, ts, cols, hot);
	} else if (hot != prevHot) {
		// Only the tile that was lit and the one that is lit now change.
		Tile(prevHot % cols, prevHot / cols, ts, cols, hot);
		Tile(hot % cols, hot / cols, ts, cols, hot);
	}
	prevHot = hot;

	// Text readout on a white box.
	FntSetFont(stdFont);
	boxH = FntLineHeight() * (
#ifdef DEBUG
		7
#else
		6
#endif
		) + 2;
	Rect(0, 0, w, boxH, gBack, false);
	if (gNewWin)
		WinSetTextColor(gText);
	y = 1;
	StrPrintF(s, "OS %d.%d cpu %lx wv %lu", (Int16)sysGetROMVerMajor(d->rom),
		(Int16)sysGetROMVerMinor(d->rom), d->cpu, d->winVersion);
	Line(s, 1, &y);
	StrPrintF(s, "heap %luK free of %luK", d->heapFree / 1024, d->heapTotal / 1024);
	Line(s, 1, &y);
	StrPrintF(s, "depth %u mask %lx col %d", gDepth, d->depthMask, (Int16)d->hasColor);
	Line(s, 1, &y);
	StrPrintF(s, "dens %lu %dx%d tile %d", d->density, (Int16)w, (Int16)h, (Int16)ts);
	Line(s, 1, &y);
	StrPrintF(s, "sony %d hr %d pins %d", (Int16)d->sony, (Int16)d->sonyHR,
		(Int16)d->pins);
	Line(s, 1, &y);
	HexStr(checksum, hex);
	StrPrintF(s, "tick %lu %s", tick, hex);
	Line(s, 1, &y);
#ifdef DEBUG
	StrPrintF(s, "t %lu.%02lu d %lu.%02lu ms", TimingTickX100() / 100,
		TimingTickX100() % 100, TimingDrawX100() / 100, TimingDrawX100() % 100);
	Line(s, 1, &y);
#endif

	if (gHiRes)
		WinSetCoordinateSystem(oldCoord);
}
