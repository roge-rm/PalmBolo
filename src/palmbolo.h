// Shared declarations for the Palm app.

#ifndef PALMBOLO_H
#define PALMBOLO_H

#include <PalmOS.h>

struct GameState;

// Go in front of a function to put it in another code segment. Each
// segment must stay under 32 KB. The game rules, main.c and the map store
// are in the first; drawing, map loading, the log and the timers are in
// "ui". A function's prototype must carry the same attribute, or a call
// from another segment jumps to the wrong place (tools/segcheck.py checks).
#ifndef SEC_DETECT
#define SEC_DETECT __attribute__((section("detect")))
#endif
#define SEC_UI __attribute__((section("ui")))

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
void SEC_UI HexStr(UInt32 v, Char *out);

#ifdef DEBUG
#include <stdarg.h>
#define kLogMaxRecords  500

// Appends a line "seconds ticks text" to PalmBoloLog. Supports %s %d %ld %lu %lx.
void SEC_UI LogF(const Char *fmt, ...);
void SEC_UI LogClear(void);
// Logs every detected value.
void SEC_UI LogDevice(const DeviceInfo *d);

// Tick and draw timer. Times are in 1/100 ms.
void SEC_UI TimingInit(UInt16 ticksPerSecond);
void SEC_UI TimingTicks(UInt32 startTicks, UInt32 endTicks, UInt16 count);
void SEC_UI TimingDraw(UInt32 startTicks, UInt32 endTicks);
UInt32 SEC_UI TimingTickX100(void);
UInt32 SEC_UI TimingDrawX100(void);
// Logs the averages about every 5 seconds.
void SEC_UI TimingMaybeLog(void);
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
void SEC_UI ScreenInit(const DeviceInfo *d);
void SEC_UI ScreenExit(void);
Boolean SEC_UI ScreenHiRes(void);      // drawing in native (double density) pixels
Boolean SEC_UI ScreenNewWin(void);     // OS 3.5 or later
UInt16 SEC_UI ScreenDepth(void);
// Palette index closest to a colour. Before OS 3.5: 0 white, 1 black.
IndexedColorType SEC_UI ScreenIndex(UInt8 r, UInt8 g, UInt8 b);

// Maps. Index 0 of the list is Everard Island, built into the app, then
// the databases of type Map, creator Rmrf.
#define kMapType        'Map '
#define kMapCreator     'Rmrf'
#define kMapResType     'Mapd'
#define kMapNameLen     32
#define kMaxMaps        24
// Fills names with the map names and returns how many (at most kMaxMaps).
UInt16 SEC_UI MapListBuild(Char names[][kMapNameLen]);
// Loads map number index of the list (name is its name) into the game.
// Returns a map error code (MAP_OK is 0), or -1 when it can't be opened.
Int16 SEC_UI MapLoad(UInt16 index, const Char *name, struct GameState *game);

// Tile view. ViewOpen sizes the view to the window and centres it on the
// tank, ViewClose gives back the input area. ViewFrame follows the tank and
// draws what changed when the game ticked, or everything when full.
void SEC_UI ViewOpen(const DeviceInfo *d, const struct GameState *game);
void SEC_UI ViewClose(void);
void SEC_UI ViewResized(void);
void SEC_UI ViewFrame(Boolean full, Boolean ticked);
Boolean SEC_UI ViewSetInputArea(Boolean closed);
// The builder action shown chosen in the button row (0 tree to 4 mine).
void SEC_UI ViewSetAction(UInt8 action);
// A tap at x, y (standard coordinates): the button number 0-6 when it is
// on the button row (0-4 the actions, 5 gunsight nearer, 6 further), else
// -1 with the map square in mx, my.
Int16 SEC_UI ViewTap(Coord x, Coord y, UInt8 *mx, UInt8 *my);
#ifdef DEBUG
// Draws the whole view count times and logs the average time.
void SEC_UI ViewTimeFull(UInt16 count);
#endif

#ifdef DEBUG
// Checksum of a const table read from the detect segment.
UInt32 SEC_DETECT DetectTableCheck(void);
#define DETECT_TABLE_CHECK 0x6C70CAF3UL
#endif

// Called by the map list when a map is chosen.
void AppOpenMap(UInt16 index, const Char *name);

#endif
