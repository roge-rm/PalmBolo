// Screen: depth and coordinate system setup.
#include "palmbolo.h"
#include "sonyhr.h"

static UInt16 gSonyRef;
static Boolean gSony, gSonyLoaded, gSonyOpen, gSonyMode, gNative;
static UInt32 gSonyWidth, gSonyHeight, gSonyDepth;
static Boolean gSonyColour;
static FontID gNativeFont;
static UInt32 gOldDepth;
static Boolean gDepthSet;
static Boolean gHiRes;          // drawing in the native (double density) system
static Boolean gNewWin;         // OS 3.5 or later: colour and screen mode calls
static Boolean gOs40;           // OS 4.0 or later
static UInt16 gDepth = 1;

IndexedColorType SEC_UI ScreenIndex(UInt8 r, UInt8 g, UInt8 b)
{
	RGBColorType c;

	// Before OS 3.5 there are no colours: 0 is white and 1 is black.
	if (!gNewWin)
		return (r * 3 + g * 6 + b) / 10 >= 128 ? 0 : 1;
	c.index = 0;
	c.r = r;
	c.g = g;
	c.b = b;
	return WinRGBToIndex(&c);
}

// Only remove a library loaded here, after its last open reference closes.
static void SEC_COLD SonyClose(void)
{
	Err err = errNone;

	if (gSonyMode)
		HRWinScreenMode(gSonyRef, winScreenModeSet, &gSonyWidth,
			&gSonyHeight, &gSonyDepth, &gSonyColour);
	if (gSonyOpen)
		err = HRClose(gSonyRef);
	if (gSonyLoaded && err == errNone)
		SysLibRemove(gSonyRef);
	gSony = gSonyLoaded = gSonyOpen = gSonyMode = false;
}

static Boolean SEC_COLD SonyInit(const DeviceInfo *d)
{
	UInt16 version = 0;
	UInt32 w = 320, h = 320;
	Err err;

	if (d->winVersion >= 4 || (!d->sony && !d->sonyHR))
		return false;
	err = SysLibFind("Sony HR Library", &gSonyRef);
	if (err == sysErrLibNotFound) {
		err = SysLibLoad('libr', 'SlHr', &gSonyRef);
		gSonyLoaded = err == errNone;
	}
	if (err != errNone)
		return false;
	if (HROpen(gSonyRef) != errNone) {
		SonyClose();
		return false;
	}
	gSonyOpen = true;
	if (HRGetAPIVersion(gSonyRef, &version) != errNone || version == 0 ||
		HRWinScreenMode(gSonyRef, winScreenModeGet, &gSonyWidth,
			&gSonyHeight, &gSonyDepth, &gSonyColour) != errNone) {
		SonyClose();
		return false;
	}
	if (HRWinScreenMode(gSonyRef, winScreenModeSet, &w, &h, NULL, NULL) != errNone) {
		SonyClose();
		return false;
	}
	gSonyMode = true;
	gSony = true;
	LOG(("Sony HR %x: native 320 x 320", version));
	return true;
}

void SEC_COLD ScreenInit(const DeviceInfo *d)
{
	UInt32 depth;
	UInt16 i;

	gNewWin = d->rom >= kRom35;
	gOs40 = d->rom >= sysMakeROMVersion(4, 0, 0, sysROMStageRelease, 0);
	gHiRes = d->winVersion >= 4 && d->density == kDensityDouble;
	if (!gHiRes && SonyInit(d))
		gHiRes = true;
	gDepth = 1;

	if (d->rom >= kMinRomVersion) {
		// Deepest supported depth up to 8 bits.
		depth = 1;
		for (i = 8; i >= 1; i--) {
			if (d->depthMask & (1UL << (i - 1))) {
				depth = i;
				break;
			}
		}
#if defined(DEBUG) && defined(FORCE_DEPTH)
		depth = FORCE_DEPTH;
#endif
		if (WinScreenMode(winScreenModeGet, NULL, NULL, &gOldDepth, NULL) == errNone) {
			if (WinScreenMode(winScreenModeSet, NULL, NULL, &depth, NULL) == errNone) {
				gDepthSet = true;
				gDepth = depth;
			} else {
				gDepth = gOldDepth;
			}
		}
	}
}

void SEC_COLD ScreenExit(void)
{
	ScreenNative(false);
	if (gDepthSet) {
		UInt32 depth = gOldDepth;

		WinScreenMode(winScreenModeSet, NULL, NULL, &depth, NULL);
		gDepthSet = false;
	}
	SonyClose();
	gHiRes = false;
}

// File scope: a function-local const table in a segment other than the
// first is reached through A5 and reads garbage.
static const Char digits[] = "0123456789ABCDEF";

void SEC_COLD HexStr(UInt32 v, Char *out)
{
	Int16 i;

	for (i = 7; i >= 0; i--) {
		out[i] = digits[v & 15];
		v >>= 4;
	}
	out[8] = 0;
}


Boolean SEC_UI ScreenHiRes(void) { return gHiRes; }
Boolean SEC_UI ScreenNewWin(void) { return gNewWin; }
Boolean SEC_UI ScreenOs40(void) { return gOs40; }
UInt16 SEC_UI ScreenDepth(void) { return gDepth; }

void SEC_UI ScreenDisplayRect(RectangleType *r)
{
	r->topLeft.x = 0;
	r->topLeft.y = 0;
	WinGetDisplayExtent(&r->extent.x, &r->extent.y);
}

Boolean SEC_UI ScreenSony(void) { return gSony; }
Boolean SEC_DRAW ScreenSonyNative(void) { return gSony && gNative; }
UInt16 SEC_DRAW ScreenSonyRef(void) { return gSonyRef; }

void SEC_UI ScreenNative(Boolean on)
{
	if (gSony && on != gNative) {
		if (on) {
			gNativeFont = FntGetFont();
			HRFntSetFont(gSonyRef, gNativeFont + 8);
		} else {
			FntSetFont(gNativeFont);
		}
	}
	gNative = on;
}
