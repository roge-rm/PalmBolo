// Shared declarations for the Palm app.

#ifndef PALMBOLO_H
#define PALMBOLO_H

#include <PalmOS.h>
#include <DLServer.h>

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
#define SEC_FRONT __attribute__((section("front")))
#define SEC_KEYS __attribute__((section("keys")))
#define SEC_DRAW __attribute__((section("draw")))

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

// Tick and draw timer, in system ticks.
void SEC_UI TimingInit(UInt16 ticksPerSecond);
void SEC_UI TimingTick(UInt32 ticks);
void SEC_UI TimingDraw(UInt32 startTicks, UInt32 endTicks);
// Time getting and handling one event.
void SEC_UI TimingEvent(UInt32 ticks);
// Logs the free heap at where, and keeps the least seen.
void SEC_UI TimingHeap(const Char *where);
// True about every 5 seconds, when TimingMaybeLog has something to log.
Boolean SEC_UI TimingDue(void);
// Logs averages, worst times, frame rate and heap, and starts the next
// period after the logging, so its own time is left out.
void SEC_UI TimingMaybeLog(void);
#define LOG(args) LogF args
#else
#define LOG(args) ((void)0)
#define TimingInit(t) ((void)0)
#define TimingTick(a) ((void)0)
#define TimingDraw(a, b) ((void)0)
#define TimingEvent(a) ((void)0)
#define TimingHeap(a) ((void)0)
#define TimingMaybeLog() ((void)0)
#endif

// A window's pixels for drawing into directly (blit.c), OS 3.0 to 3.3 at
// 1 and 2 bits only. bits is NULL when the OS calls must be used.
typedef struct BlitWin {
	UInt8 *bits;        // the window's top left pixel
	UInt16 rowBytes, depth;
	Coord w, h;
	WinHandle win;
} BlitWin;
Boolean SEC_UI BlitFind(WinHandle w, BlitWin *b);
void SEC_UI BlitCell(const BlitWin *src, Coord sx, Coord sy, const BlitWin *dst,
	Coord x, Coord y, const RectangleType *clip);
void SEC_UI BlitSprite(const BlitWin *mask, Coord mx, Coord my, const BlitWin *spr,
	Coord sx, Coord sy, const BlitWin *dst, Coord x, Coord y, const RectangleType *clip);

// Screen. ScreenInit picks the depth and coordinate system, ScreenExit
// restores them.
void SEC_UI ScreenInit(const DeviceInfo *d);
void SEC_UI ScreenExit(void);
Boolean SEC_UI ScreenHiRes(void);      // drawing in native (double density) pixels
Boolean SEC_UI ScreenNewWin(void);     // OS 3.5 or later
UInt16 SEC_UI ScreenDepth(void);
Boolean SEC_UI ScreenOs40(void);       // OS 4.0 or later
// The whole display in the current coordinate system.
void SEC_UI ScreenDisplayRect(RectangleType *r);
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
// tank, or shows an alert and returns false if it can't draw (no memory for
// the art, a 16-bit screen). ViewClose gives back the input area.
// ViewFrame follows the tank and draws what changed when the game ticked,
// or everything when full.
Boolean SEC_UI ViewOpen(const DeviceInfo *d, const struct GameState *game);
void SEC_UI ViewClose(void);
void SEC_UI ViewResized(void);
void SEC_UI ViewFrame(Boolean full, Boolean ticked);
Boolean SEC_UI ViewSetInputArea(Boolean closed);
void SEC_UI ViewPins(Boolean on);
void SEC_UI ViewFormInputArea(Boolean closed);
void SEC_UI ViewInputTrigger(void);
// The builder action shown chosen in the button row (0 tree to 4 mine).
void SEC_UI ViewSetAction(UInt8 action);
// Draws an icon cell of the art (a whole cell, no mask) at x, y.
void SEC_UI ViewIcon(UInt16 cell, Coord x, Coord y);
// A tap at x, y (standard coordinates): the button number 0-6 when it is
// on the button row (0-4 the actions, 5 gunsight nearer, 6 further),
// kTapPanel on the status panel (or on the overview when it covers the
// view), else -1 with the map square in mx, my.
#define kTapPanel 7
Int16 SEC_UI ViewTap(Coord x, Coord y, UInt8 *mx, UInt8 *my);
#ifdef DEBUG
// Draws the whole view count times and logs the average time.
void SEC_UI ViewTimeFull(UInt16 count);
UInt16 SEC_UI ViewCheck(void);
#endif

// Status panel and overview map (panel.c). The view calls these.
// PanelLayout takes the window width and returns the panel's height.
// PanelDraw takes the top of the panel and the view in world pixels.
Coord SEC_UI PanelLayout(Coord w);
void SEC_UI PanelOpen(const DeviceInfo *d, const struct GameState *game);
void SEC_UI PanelClose(void);
void SEC_UI PanelSquare(UInt8 x, UInt8 y);
void SEC_UI PanelItem(UInt8 x, UInt8 y);
void SEC_UI PanelRebuild(void);
Boolean SEC_UI PanelCovers(void);
// The colour of the panel and the button bar.
IndexedColorType SEC_UI PanelBg(void);
Boolean SEC_UI PanelToggle(void);
void SEC_UI PanelDraw(Boolean force, Coord py, Int32 vx, Int32 vy, Coord vw, Coord vh, Coord ts);

// The view composed offscreen (compose.c), OS 3.5 and later. CompFit makes
// the window, CompWin is it or NULL, CompAdd and CompAll note what changed
// this frame (from CompBegin) and CompFlush copies that to dst.
Boolean SEC_DRAW CompFit(Coord w, Coord h);
void SEC_DRAW CompFree(void);
WinHandle SEC_DRAW CompWin(void);
void SEC_DRAW CompBegin(void);
void SEC_DRAW CompAdd(const RectangleType *r);
void SEC_DRAW CompAll(void);
void SEC_DRAW CompFlush(WinHandle dst);

// Keyboard (keys.c). KeysStart and KeysStop go round a game showing; they
// set and put back the key repeat. KeysEvent takes a key event and returns
// a KA_* action, with KA_EAT when the event is used up. KeysHeld gives the
// IN_* keys held now. KeysTarget is the square under the gunsight.
enum { KA_NONE, KA_TREE, KA_ROAD, KA_WALL, KA_PILL, KA_MINE, KA_NEAR, KA_FAR, KA_OVERVIEW,
	KA_MENU };
#define KA_EAT 0x80
void SEC_KEYS KeysStart(UInt16 ticksPerSecond);
void SEC_KEYS KeysStop(void);
void SEC_KEYS KeysClear(void);
UInt8 SEC_KEYS KeysEvent(const EventType *e);
UInt8 SEC_KEYS KeysHeld(void);
void SEC_KEYS KeysTarget(const struct GameState *g, UInt8 *mx, UInt8 *my);

// Sound (sound.c). SoundOpen reads the system game volume when the game
// shows; SoundFrame plays the sounds the rules made since the last frame
// and moves a playing sound on, as heard by the viewer (game_viewer).
void SEC_UI SoundOpen(UInt16 ticksPerSecond);
void SEC_UI SoundSetOn(Boolean on);
Boolean SEC_UI SoundIsOn(void);
void SEC_UI SoundFrame(const struct GameState *g);
void SEC_UI SoundStop(void);

#ifdef DEBUG
// Checksum of a const table read from the detect segment.
UInt32 SEC_DETECT DetectTableCheck(void);
#define DETECT_TABLE_CHECK 0x6C70CAF3UL
#endif

// The app's own settings, kept in its preferences (main.c). Settings the
// host picks for a game and the player's own.
#define kNameLen        16
typedef struct AppPrefs {
	UInt8 sound;        // 1 on
	UInt8 autoSlow;
	UInt8 autoSight;
	UInt8 gameType;     // GAME_OPEN..GAME_STRICT
	UInt8 hiddenMines;
	UInt8 timeLimit;    // index of the choices, 0 off
	UInt8 numBots;      // 0 to 7
	UInt8 botLevel;     // 0 easy, 1 normal, 2 hard
	UInt8 teams;        // 1 for teams
	UInt8 keys[4];      // what hard buttons 1 to 4 do, KEY_* below
	UInt8 pad;
	Char name[kNameLen];
	Char map[kMapNameLen];
} AppPrefs;
enum { KEY_NONE, KEY_LEFT, KEY_RIGHT, KEY_FIRE, KEY_MINE, KEY_FASTER, KEY_SLOWER, KEY_KINDS };
AppPrefs *AppPrefsGet(void);
void AppPrefsSave(void);
void AppSetSound(Boolean on);

// Starts a game on map index (named name) with the prefs' settings. Replaces
// the saved game.
void AppOpenMap(UInt16 index, const Char *name);
// Carries on the saved game.
void AppResume(void);
// Minutes of the time limit choice (0 off) .
UInt16 AppTimeMinutes(UInt8 choice);

// The start screens and the saved game (front.c, its own segment). The
// handler is for the form with that ID, or NULL.
FormEventHandlerType *SEC_FRONT FrontHandler(UInt16 formId);
// Shows the results of a game that ended and goes on to the title.
void SEC_FRONT FrontResults(const struct GameState *g);
// The saved game: the game state in a database of its own and the map
// store kept beside it.
Boolean SEC_FRONT SaveExists(void);
Boolean SEC_FRONT SaveWrite(const struct GameState *g);
// Reads the save into g and opens its map as the store; the save is then
// the game in progress until written again.
Boolean SEC_FRONT SaveRead(struct GameState *g);
void SEC_FRONT SaveDrop(void);
#if defined(DEBUG) && defined(DEMO_SHOWCASE)
// Sets up a new game so the parachute, the builder walking, carried pills
// and a wreck are on screen (EXTRA=-DDEMO_SHOWCASE).
void SEC_FRONT DemoShowcase(struct GameState *g);
#endif
#ifdef DEBUG
// Plays a game straight through and saved and resumed halfway, and logs
// whether the checksums match. Replaces any saved game.
void SEC_FRONT FrontSaveTest(struct GameState *g);
#endif

#endif
