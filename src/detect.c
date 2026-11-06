#include "palmbolo.h"

#define kSonyCreator  'SoNy'

static UInt32 GetFtr(UInt32 creator, UInt16 num, UInt32 def)
{
	UInt32 v;

	if (FtrGet(creator, num, &v) != errNone)
		return def;
	return v;
}

void SEC_DETECT DeviceDetect(DeviceInfo *d)
{
	UInt16 heap = MemHeapID(0, 0);
	UInt16 ref;
	UInt32 depths = 1, v;
	Boolean color = false;

	MemSet(d, sizeof(*d), 0);
	d->rom = GetFtr(sysFtrCreator, sysFtrNumROMVersion, 0);
	d->cpu = GetFtr(sysFtrCreator, sysFtrNumProcessorID, 0);
	d->ticksPerSecond = SysTicksPerSecond();

	MemHeapFreeBytes(heap, &d->heapFree, &d->heapMax);
	d->heapTotal = MemHeapSize(heap);

	// WinScreenMode is ScrDisplayMode on OS 3.0 to 3.3 (the same trap),
	// with 1 and 2 bits on grey Palms. The window manager version is 3.5.
	if (d->rom >= kRom35)
		d->winVersion = GetFtr(sysFtrCreator, sysFtrNumWinVersion, 0);
	if (d->rom >= kMinRomVersion) {
		if (WinScreenMode(winScreenModeGetSupportedDepths, NULL, NULL,
		        &depths, NULL) != errNone)
			depths = 1;
		if (WinScreenMode(winScreenModeGetSupportsColor, NULL, NULL, NULL,
		        &color) != errNone)
			color = false;
	}
	d->depthMask = depths;
	d->hasColor = color;

	// The high density calls only exist with window manager 4 or later.
	d->density = kDensityLow;
	if (d->winVersion >= 4) {
		if (WinScreenGetAttribute(winScreenDensity, &v) == errNone)
			d->density = v;
	}

	d->sony = FtrGet(kSonyCreator, 1, &v) == errNone;
	d->sonyHR = SysLibFind("Sony HR Lib", &ref) == errNone;
	d->pins = FtrGet(pinCreator, pinFtrAPIVersion, &v) == errNone;
}

#ifdef DEBUG
// A const table read from this segment, for the self-test that const data
// reads back right from a code segment other than the first.
static const UInt8 kProbe[64] = {
	0x0B, 0x30, 0x55, 0x7A, 0x9F, 0xC4, 0xE9, 0x0E,
	0x33, 0x58, 0x7D, 0xA2, 0xC7, 0xEC, 0x11, 0x36,
	0x5B, 0x80, 0xA5, 0xCA, 0xEF, 0x14, 0x39, 0x5E,
	0x83, 0xA8, 0xCD, 0xF2, 0x17, 0x3C, 0x61, 0x86,
	0xAB, 0xD0, 0xF5, 0x1A, 0x3F, 0x64, 0x89, 0xAE,
	0xD3, 0xF8, 0x1D, 0x42, 0x67, 0x8C, 0xB1, 0xD6,
	0xFB, 0x20, 0x45, 0x6A, 0x8F, 0xB4, 0xD9, 0xFE,
	0x23, 0x48, 0x6D, 0x92, 0xB7, 0xDC, 0x01, 0x26
};

UInt32 DetectTableCheck(void)
{
	UInt32 h = 0x12345678UL;
	UInt16 i;

	for (i = 0; i < 64; i++)
		h = ((h << 5) | (h >> 27)) ^ kProbe[i];
	return h;
}
#endif
