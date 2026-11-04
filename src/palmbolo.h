// Shared declarations for the Palm app.

#ifndef PALMBOLO_H
#define PALMBOLO_H

#include <PalmOS.h>

// Goes in front of a function to put it in the second code segment. Each
// segment must stay under 32 KB.
#ifndef SEC_DETECT
#define SEC_DETECT __attribute__((section("detect")))
#endif

#define kMinRomVersion  sysMakeROMVersion(3, 0, 0, sysROMStageRelease, 0)
#define kRom35          sysMakeROMVersion(3, 5, 0, sysROMStageRelease, 0)

// What DeviceDetect finds out about the device.
typedef struct DeviceInfo {
	UInt32 rom;          // sysFtrNumROMVersion
	UInt32 cpu;          // sysFtrNumProcessorID
	UInt32 winVersion;   // sysFtrNumWinVersion, 0 before OS 3.5 or 4
	UInt32 heapFree;     // dynamic heap, bytes
	UInt32 heapMax;      // largest free chunk
	UInt32 heapTotal;
	UInt32 depthMask;    // bit n-1 set when depth n is supported
	UInt32 density;      // 72 normal, 144 double, 0 if unknown
	UInt16 ticksPerSecond;
	Boolean hasColor;
	Boolean sony;        // Sony feature present
	Boolean sonyHR;      // Sony HR library present (detected only)
	Boolean pins;        // collapsible input area (detected only)
} DeviceInfo;

// Fills in the device values. Lives in the second code segment, so callers
// must see this declaration with SEC_DETECT.
void SEC_DETECT DeviceDetect(DeviceInfo *d);

// Upper-case hex of v, padded to 8 digits. out needs 9 bytes.
void HexStr(UInt32 v, Char *out);

#ifdef DEBUG
#include <stdarg.h>
#define kLogMaxRecords  500

// Appends a line "seconds ticks text" to PalmBoloLog. Supports %s %d %ld %lu %lx.
void LogF(const Char *fmt, ...);
void LogClear(void);
// Logs every detected value.
void LogDevice(const DeviceInfo *d);

// Tick and draw timer. Times are in 1/100 ms.
void TimingInit(UInt16 ticksPerSecond);
void TimingTicks(UInt32 startTicks, UInt32 endTicks, UInt16 count);
void TimingDraw(UInt32 startTicks, UInt32 endTicks);
UInt32 TimingTickX100(void);
UInt32 TimingDrawX100(void);
// Logs the averages about every 5 seconds.
void TimingMaybeLog(void);
#define LOG(args) LogF args
#else
#define LOG(args) ((void)0)
#define TimingInit(t) ((void)0)
#define TimingTicks(a, b, c) ((void)0)
#define TimingDraw(a, b) ((void)0)
#define TimingMaybeLog() ((void)0)
#endif

// Screen. ScreenInit picks the depth and coordinate system, ScreenExit
// restores them.
void ScreenInit(const DeviceInfo *d);
void ScreenExit(void);
// Draws the test screen: everything when full, else only what changed.
void ScreenDraw(const DeviceInfo *d, UInt32 tick, UInt32 checksum, Boolean full);

#endif
