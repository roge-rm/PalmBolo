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

	// The window manager version feature and WinScreenMode are OS 3.5.
	if (d->rom >= kRom35) {
		d->winVersion = GetFtr(sysFtrCreator, sysFtrNumWinVersion, 0);
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
