// Screen: depth and coordinate system setup.
#include "palmbolo.h"

static UInt32 gOldDepth;
static Boolean gDepthSet;
static Boolean gHiRes;          // drawing in the native (double density) system
static Boolean gNewWin;         // OS 3.5 or later: colour and screen mode calls
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

void SEC_UI ScreenInit(const DeviceInfo *d)
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
	}
}

void SEC_UI ScreenExit(void)
{
	if (gDepthSet) {
		UInt32 depth = gOldDepth;

		WinScreenMode(winScreenModeSet, NULL, NULL, &depth, NULL);
		gDepthSet = false;
	}
}

// File scope: a function-local const table in a segment other than the
// first is reached through A5 and reads garbage.
static const Char digits[] = "0123456789ABCDEF";

void SEC_UI HexStr(UInt32 v, Char *out)
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
UInt16 SEC_UI ScreenDepth(void) { return gDepth; }
