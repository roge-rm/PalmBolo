// The map editor: a copy of a map from the map list, scrolled with the
// hardware buttons or by dragging with the pan tool, and painted a square
// at a time. It's saved as a Map database the map list shows and beams.
//
// The picker under the swatch button chooses what the Paint button does:
// paint a terrain, place a pill, base or start, or erase items. With an
// item chosen, tapping an empty square places one and tapping any item
// chooses it; either can then be dragged. Holding the pen still on an item
// opens its settings, as does Item settings in the menu. Undo takes back
// the last stroke, fill, box or item change.
//
// Tapping the Paint button again picks the tool for terrain: a brush of
// 1, 3 or 5 squares, Fill or Box. Zoom shows the whole map, where the
// tools paint too and a tap with Pan goes back to the normal view there.
// Test play runs the map with three bots and comes back to the editor,
// the map kept in the draft database meanwhile. Unsaved work goes in the
// draft when the app stops too, and the next launch opens it again.
//
// The map is loaded by MapLoad as for a game, the terrain into the map
// store and the pills, bases and starts into the game state's MapInfo; the
// rest of the game state is cleared. The saved game has a store of its own
// and isn't touched. The view is the game's (tileview.c) in its edit mode.
#include "palmbolo.h"
#include "resource.h"
#include "game/game.h"
#include "game/mapstore.h"
#include "game/mapedit.h"

#define kEditVersion    1       // database version of maps the editor saved
#define kStageName      "PalmBolo editor"
#define kStageType      'Medt'
#define kChunk          4096    // record size, as an imported map
#define kKeyStep        32      // standard pixels a button press scrolls, 4 squares
#define kHoldTenths     6       // holding the pen on an item this long opens its settings
#define kUndoName       "PalmBolo undo"     // the whole-map copy for undo
#define kDraftName      "PalmBolo draft"    // the map over test play or app stop
#define kOldName        "PalmBolo old"      // a map being replaced by a save
#define kDraftVersion   1
#define kRows           16      // map rows to a record in both
#define kRecs           (256 / kRows)

// The draft's last record: this, then the MapInfo.
typedef struct DraftHead {
	UInt16 version;             // kDraftVersion
	UInt16 infoSize;            // sizeof(MapInfo)
	UInt16 index;
	UInt8 fresh, dirty;
	UInt8 cx, cy;               // the middle of the view
	Char name[kMapNameLen];
} DraftHead;

// After the MapInfo in newer drafts: the tools as they were. A draft
// without it loads with the defaults.
typedef struct DraftTools {
	UInt8 tool, last, brush, choice;
} DraftTools;

// Tools, and the buttons along the bottom.
enum { TOOL_PAN, TOOL_PAINT, TOOL_FILL, TOOL_BOX };
enum { BTN_PAN, BTN_TOOL, BTN_TERRAIN, BTN_ZOOM, BTN_SAVE, BTN_DONE, BTN_COUNT };

// The 5-way on the Tungsten T line: keyCode bits of vchrNavChange.
#define vchrNavChange   0x0503
#define kNavUp          0x0001
#define kNavDown        0x0002
#define kNavLeft        0x0004
#define kNavRight       0x0008
#define kNavChangeShift 8

typedef struct EditState {
	UInt16 index;               // place in the map list, 0 the built-in map
	Char name[kMapNameLen];
	UInt8 tool;
	UInt8 last;                 // the tool the Paint button picks, not TOOL_PAN
	UInt8 brush;                // 1, 3 or 5 squares
	UInt8 fresh;                // a new map's size + 1 until saved
	Boolean zoom;               // the whole map shows
	Boolean testing;            // in test play, the map kept in kDraftName
	Boolean draft;              // Open takes the map from kDraftName
	UInt8 cx, cy;               // the middle of the view over test play
	Char labels[32];            // the button row
	DmOpenRef undoDb;           // kUndoName, its records locked
	MemHandle undoH[kRecs];
	UInt8 *undoP[kRecs];
	UInt8 choice;               // picker index: map_edit_terrain, then the items
	UInt8 selKind, selIndex;    // the chosen item, selKind ITEM_NONE for none
	UInt8 viewer;               // game_viewer() to put back
	Boolean dirty;              // changed since loaded or saved
	Boolean open;               // the view is open
	Coord px, py;               // last pen point, standard coordinates
	UInt8 lx, ly;               // last square painted
	MapUndo undo;
} EditState;

static EditState *gEd;

static void * SEC_EDITOR Obj(FormType *frm, UInt16 id)
{
	return FrmGetObjectPtr(frm, FrmGetObjectIndex(frm, id));
}

static Boolean SEC_EDITOR ItemChoice(void)
{
	return gEd->choice >= MAP_EDIT_ITEM0 && gEd->choice < MAP_EDIT_REMOVE;
}

// Copies s and its 0 to p, returning the end.
static Char * SEC_EDITOR Label(Char *p, const Char *s)
{
	StrCopy(p, s);
	return p + StrLen(s) + 1;
}

static void SEC_EDITOR Bar(void)
{
	UInt8 swatch = map_edit_terrain(gEd->choice), sel;
	const Char *tool;
	Char *p = gEd->labels;

	if (gEd->choice >= MAP_EDIT_ITEM0) {
		tool = gEd->choice == MAP_EDIT_REMOVE ? "Erase" : "Place";
		swatch = (UInt8)(kSwatchPill + gEd->choice - MAP_EDIT_ITEM0);
	} else if (gEd->last == TOOL_FILL) {
		tool = "Fill";
	} else if (gEd->last == TOOL_BOX) {
		tool = "Box";
	} else {
		tool = gEd->brush == 5 ? "5x5" : gEd->brush == 3 ? "3x3" : "Paint";
	}
	p = Label(p, "Pan");
	p = Label(p, tool);
	p = Label(p, "");
	p = Label(p, "Zoom");
	p = Label(p, "Save");
	Label(p, "Done");
	sel = (UInt8)(1 << (gEd->tool == TOOL_PAN ? BTN_PAN : BTN_TOOL));
	if (gEd->zoom)
		sel |= 1 << BTN_ZOOM;
	ViewEditBar(gEd->labels, BTN_COUNT, sel, BTN_TERRAIN, swatch);
}

// Chooses an item, ITEM_NONE for none, and frames its square.
static void SEC_EDITOR Select(UInt8 kind, UInt8 index)
{
	UInt8 x = 0, y = 0;

	gEd->selKind = kind;
	gEd->selIndex = index;
	if (kind != ITEM_NONE)
		map_item_xy(&AppGame()->map, kind, index, &x, &y);
	ViewEditSelect(kind != ITEM_NONE, x, y);
}

static void SEC_EDITOR Say(const Char *text)
{
	FrmCustomAlert(EditItemAlert, text, "", "");
}

void SEC_EDITOR EditStart(UInt16 index, const Char *name)
{
	if (!gEd)
		gEd = (EditState *)MemPtrNew(sizeof(EditState));
	if (!gEd) {
		FrmCustomAlert(ViewAlert, "Not enough memory for the editor.", "", "");
		return;
	}
	MemSet(gEd, sizeof(EditState), 0);
	gEd->index = index;
	StrNCopy(gEd->name, name, kMapNameLen - 1);
	gEd->name[kMapNameLen - 1] = 0;
	gEd->tool = gEd->last = TOOL_PAINT;
	gEd->brush = 1;
	gEd->selKind = ITEM_NONE;
	FrmGotoForm(EditForm);
}

void SEC_EDITOR EditNew(void)
{
	FormType *old = FrmGetActiveForm(), *frm = FrmInitForm(EditSizeForm);
	UInt16 b;

	if (!frm)
		return;
	FrmSetActiveForm(frm);
	ViewDialogInputArea(frm);
	b = FrmDoDialog(frm);
	FrmDeleteForm(frm);
	if (old)
		FrmSetActiveForm(old);
	if (b < EditSizeSmall || b > EditSizeLarge) {
		MapListPick("");
		return;
	}
	EditStart(0, "New map");
	if (gEd)
		gEd->fresh = (UInt8)(b - EditSizeSmall + 1);
}

// ---- the databases: the whole-map copy for undo and the draft ----

static void SEC_EDITOR Delete(const Char *name)
{
	LocalID id = DmFindDatabase(0, (Char *)name);

	if (id)
		DmDeleteDatabase(0, id);
}

// A new database of kRecs records of kRows map rows and then a record of
// extra bytes if extra isn't 0, open to write. NULL without room.
static DmOpenRef SEC_EDITOR MakeDb(const Char *name, UInt16 extra)
{
	DmOpenRef db = NULL;
	LocalID id;
	UInt16 i, rec, n = extra ? kRecs + 1 : kRecs;
	MemHandle h;

	Delete(name);
	if (DmCreateDatabase(0, name, kMapCreator, kStageType, false) != errNone)
		return NULL;
	id = DmFindDatabase(0, (Char *)name);
	if (id)
		db = DmOpenDatabase(0, id, dmModeReadWrite);
	for (i = 0; db && i < n; i++) {
		rec = dmMaxRecordIndex;
		h = DmNewRecord(db, &rec, i < kRecs ? (UInt32)kRows * 256 : extra);
		if (!h) {
			DmCloseDatabase(db);
			db = NULL;
			break;
		}
		DmReleaseRecord(db, rec, true);
	}
	if (!db && id)
		DmDeleteDatabase(0, id);
	return db;
}

int SEC_EDITOR map_snap_alloc(void)
{
	UInt16 i;

	if (gEd->undoDb)
		return 0;
	gEd->undoDb = MakeDb(kUndoName, 0);
	if (!gEd->undoDb)
		return 1;
	for (i = 0; i < kRecs; i++) {
		gEd->undoH[i] = DmQueryRecord(gEd->undoDb, i);
		gEd->undoP[i] = (UInt8 *)MemHandleLock(gEd->undoH[i]);
	}
	return 0;
}

void SEC_EDITOR map_snap_put(uint8_t y, const uint8_t *row)
{
	DmWrite(gEd->undoP[y / kRows], (UInt32)(y % kRows) * 256, row, 256);
}

void SEC_EDITOR map_snap_set(uint8_t x, uint8_t y, uint8_t t)
{
	DmWrite(gEd->undoP[y / kRows], (UInt32)(y % kRows) * 256 + x, &t, 1);
}

const uint8_t *SEC_EDITOR map_snap_row(uint8_t y)
{
	return gEd->undoP[y / kRows] + (UInt16)(y % kRows) * 256;
}

static void SEC_EDITOR UndoFree(void)
{
	UInt16 i;

	if (!gEd->undoDb)
		return;
	for (i = 0; i < kRecs; i++)
		if (gEd->undoP[i])
			MemHandleUnlock(gEd->undoH[i]);
	DmCloseDatabase(gEd->undoDb);
	gEd->undoDb = NULL;
	Delete(kUndoName);
}

// Keeps the terrain, the items and what the editor knows of the map in
// kDraftName, with cx, cy the middle of the view. False without room.
static Boolean SEC_EDITOR DraftKeep(UInt8 cx, UInt8 cy)
{
	DmOpenRef db = MakeDb(kDraftName, sizeof(DraftHead) + sizeof(MapInfo) + sizeof(DraftTools));
	DraftHead head;
	DraftTools tools;
	UInt16 i, y;
	MemHandle h;
	void *p;

	if (!db)
		return false;
	MemSet(&head, sizeof(head), 0);
	head.version = kDraftVersion;
	head.infoSize = sizeof(MapInfo);
	head.index = gEd->index;
	head.fresh = gEd->fresh;
	head.dirty = gEd->dirty;
	head.cx = cx;
	head.cy = cy;
	StrCopy(head.name, gEd->name);
	tools.tool = gEd->tool;
	tools.last = gEd->last;
	tools.brush = gEd->brush;
	tools.choice = gEd->choice;
	for (i = 0; i <= kRecs; i++) {
		h = DmGetRecord(db, i);
		p = MemHandleLock(h);
		if (i < kRecs) {
			for (y = 0; y < kRows; y++)
				DmWrite(p, (UInt32)y * 256, mapstore_row(i * kRows + y), 256);
		} else {
			DmWrite(p, 0, &head, sizeof(head));
			DmWrite(p, sizeof(head), &AppGame()->map, sizeof(MapInfo));
			DmWrite(p, sizeof(head) + sizeof(MapInfo), &tools, sizeof(tools));
		}
		MemHandleUnlock(h);
		DmReleaseRecord(db, i, true);
	}
	DmCloseDatabase(db);
	return true;
}

// Puts back what DraftKeep kept, in a fresh map store, and deletes it.
// Returns 0, 1 when the draft is gone or unreadable, or 2 without room
// for the store, the draft kept to try again.
static UInt8 SEC_EDITOR DraftBack(GameState *g)
{
	LocalID id = DmFindDatabase(0, kDraftName);
	DmOpenRef db = id ? DmOpenDatabase(0, id, dmModeReadOnly) : NULL;
	MemHandle h = db && DmNumRecords(db) == kRecs + 1 ? DmQueryRecord(db, kRecs) : NULL;
	const DraftHead *head;
	UInt8 res = 1;
	UInt16 i, y;
	const UInt8 *p;

	UInt32 size = h ? MemHandleSize(h) : 0;

	if (size == sizeof(DraftHead) + sizeof(MapInfo) ||
			size == sizeof(DraftHead) + sizeof(MapInfo) + sizeof(DraftTools)) {
		head = (const DraftHead *)MemHandleLock(h);
		if (head->version == kDraftVersion && head->infoSize == sizeof(MapInfo)) {
			res = mapstore_open(TERRAIN_DEEP_SEA) == 0 ? 0 : 2;
			if (res == 0) {
				gEd->index = head->index;
				gEd->fresh = head->fresh;
				gEd->dirty = head->dirty;
				gEd->cx = head->cx;
				gEd->cy = head->cy;
				MemMove(gEd->name, head->name, kMapNameLen);
				gEd->name[kMapNameLen - 1] = 0;
				MemMove(&g->map, head + 1, sizeof(MapInfo));
				if (size > sizeof(DraftHead) + sizeof(MapInfo)) {
					const DraftTools *t = (const DraftTools *)((const UInt8 *)(head + 1) + sizeof(MapInfo));

					if (t->tool <= TOOL_BOX && t->last >= TOOL_PAINT && t->last <= TOOL_BOX &&
							(t->brush == 1 || t->brush == 3 || t->brush == 5) &&
							t->choice < MAP_EDIT_CHOICES) {
						gEd->tool = t->tool;
						gEd->last = t->last;
						gEd->brush = t->brush;
						gEd->choice = t->choice;
					}
				}
			}
		}
		MemHandleUnlock(h);
	}
	for (i = 0; res == 0 && i < kRecs; i++) {
		h = DmQueryRecord(db, i);
		if (!h || MemHandleSize(h) < (UInt32)kRows * 256) {
			res = 1;
			break;
		}
		p = (const UInt8 *)MemHandleLock(h);
		for (y = 0; y < kRows; y++)
			mapstore_set_bytes(0, (UInt8)(i * kRows + y), 256, p + y * 256);
		MemHandleUnlock(h);
	}
	if (db)
		DmCloseDatabase(db);
	if (res != 2)
		Delete(kDraftName);
	return res;
}

Boolean SEC_EDITOR EditResume(void)
{
	if (!DmFindDatabase(0, kDraftName))
		return false;
	EditStart(0, "");
	if (!gEd)
		return false;
	gEd->draft = true;
	gEd->viewer = game_viewer();
	return true;
}

void SEC_EDITOR EditStop(void)
{
	UInt8 cx, cy;

	if (!gEd)
		return;
	if (gEd->testing) {
		// The draft holds the map already.
		if (!gEd->dirty)
			Delete(kDraftName);
		return;
	}
	if (gEd->open && gEd->dirty) {
		ViewEditWhere(&cx, &cy);
		DraftKeep(cx, cy);
	}
}

// The view, on a map now loaded, centred on square x, y.
static void SEC_EDITOR Opened(UInt8 x, UInt8 y)
{
	game_set_viewer(0);
	game_take_changes(NULL, 0);
	gEd->zoom = false;
	if (!ViewEditOpen(AppDevice(), AppGame(), x, y)) {
		game_set_viewer(gEd->viewer);
		FrmGotoForm(MapListForm);
		return;
	}
	gEd->open = true;
	Bar();
	ViewEditFrame(true);
}

static void SEC_EDITOR Open(void)
{
	GameState *g = AppGame();
	Int16 err;
	UInt16 i;

	FrmDrawForm(FrmGetActiveForm());
	MemSet(g, sizeof(GameState), 0);
	if (gEd->draft) {
		UInt8 res = DraftBack(g);

		gEd->draft = false;
		gEd->testing = false;
		if (res != 0) {
			FrmCustomAlert(ViewAlert, res == 2 ? "Not enough memory to open the map." :
				"The map was lost.", "", "");
			FrmGotoForm(MapListForm);
			return;
		}
		Opened(gEd->cx, gEd->cy);
		return;
	}
	if (gEd->fresh) {
		err = mapstore_open(TERRAIN_DEEP_SEA) ? -1 : MAP_OK;
		if (err == MAP_OK)
			map_blank(&g->map, (UInt8)(gEd->fresh - 1));
	} else {
		err = MapLoad(gEd->index, gEd->name, g);
	}
	if (err != MAP_OK) {
		FrmCustomAlert(MapErrorAlert, gEd->name,
			err < 0 ? "Can't open it." : map_error_text(err), "");
		FrmGotoForm(MapListForm);
		return;
	}
	// Items in the mined border load inactive; here they're shown, so they
	// can be moved in.
	for (i = 0; i < g->map.numPills; i++)
		g->map.pills[i].active = 1;
	for (i = 0; i < g->map.numBases; i++)
		g->map.bases[i].active = 1;
	for (i = 0; i < g->map.numStarts; i++)
		g->map.starts[i].active = 1;
	gEd->viewer = game_viewer();
	// map_parse centres the land.
	Opened(128, 128);
}

void SEC_EDITOR EditDrop(void)
{
	if (gEd) {
		UndoFree();
		MemPtrFree(gEd);
		gEd = NULL;
	}
}

// Over test play only the view closes.
static void SEC_EDITOR Close(void)
{
	if (!gEd)
		return;
	if (gEd->open) {
		ViewClose();
		gEd->open = false;
		if (!gEd->testing)
			game_set_viewer(gEd->viewer);
		game_take_changes(NULL, 0);
	}
	if (!gEd->testing)
		EditDrop();
}

// ---- painting ----

static void SEC_EDITOR PaintAt(UInt8 x, UInt8 y)
{
	if (map_brush(&gEd->undo, &AppGame()->map, x, y, gEd->brush, map_edit_terrain(gEd->choice)))
		gEd->dirty = true;
}

// Paints every square on the line from the last square to x, y, so a quick
// drag leaves no gaps.
static void SEC_EDITOR PaintTo(UInt8 x, UInt8 y)
{
	Int16 x0 = gEd->lx, y0 = gEd->ly, dx = (Int16)x - x0, dy = (Int16)y - y0;
	Int16 sx = dx < 0 ? -1 : 1, sy = dy < 0 ? -1 : 1, err;

	if (dx < 0) dx = -dx;
	if (dy < 0) dy = -dy;
	err = dx - dy;
	for (;;) {
		PaintAt((UInt8)x0, (UInt8)y0);
		if (x0 == x && y0 == y)
			break;
		if (2 * err > -dy) {
			err -= dy;
			x0 += sx;
		}
		if (2 * err < dx) {
			err += dx;
			y0 += sy;
		}
	}
	gEd->lx = x;
	gEd->ly = y;
}

// ---- saving ----

typedef struct DbWriter {
	DmOpenRef db;
	UInt8 *buf;
	UInt16 n;
} DbWriter;

static Boolean SEC_EDITOR Flush(DbWriter *w)
{
	UInt16 rec = dmMaxRecordIndex;
	MemHandle h;

	if (!w->n)
		return true;
	h = DmNewRecord(w->db, &rec, w->n);
	if (!h)
		return false;
	DmWrite(MemHandleLock(h), 0, w->buf, w->n);
	MemHandleUnlock(h);
	DmReleaseRecord(w->db, rec, true);
	w->n = 0;
	return true;
}

static int SEC_EDITOR Put(void *ctx, const uint8_t *src, uint32_t len)
{
	DbWriter *w = (DbWriter *)ctx;

	while (len > 0) {
		UInt32 k = kChunk - w->n;

		if (k > len)
			k = len;
		MemMove(w->buf + w->n, src, k);
		w->n = (UInt16)(w->n + k);
		src += k;
		len -= k;
		if (w->n == kChunk && !Flush(w))
			return 1;
	}
	return 0;
}

// True for a Map database the editor saved, the only kind it replaces.
static Boolean SEC_EDITOR Mine(LocalID id)
{
	UInt16 version = 0;
	UInt32 type = 0, creator = 0;

	return DmDatabaseInfo(0, id, NULL, NULL, &version, NULL, NULL, NULL, NULL, NULL, NULL,
		&type, &creator) == errNone && type == kMapType && creator == kMapCreator &&
		version == kEditVersion;
}

// Names the built-in map and the app's own databases have.
static Boolean SEC_EDITOR Reserved(const Char *name)
{
	return StrCaselessCompare(name, "Everard Island") == 0 ||
		StrNCaselessCompare(name, "PalmBolo", 8) == 0;
}

// Writes the map into a new database, then gives it the name in place of
// any map of that name. The old map is renamed aside until the new one has
// the name, so a failure leaves it as it was.
static Boolean SEC_EDITOR Write(const Char *name)
{
	DbWriter w;
	LocalID id, old;
	UInt16 attr = dmHdrAttrBackup, version = kEditVersion;
	UInt32 type = kMapType;
	int err = MAP_ERR_WRITE;

	Delete(kStageName);
	if (DmCreateDatabase(0, kStageName, kMapCreator, kStageType, false) != errNone)
		return false;
	id = DmFindDatabase(0, kStageName);
	if (!id)
		return false;
	MemSet(&w, sizeof(w), 0);
	w.db = DmOpenDatabase(0, id, dmModeReadWrite);
	w.buf = (UInt8 *)MemPtrNew(kChunk);
	if (w.db && w.buf) {
		err = map_write(&AppGame()->map, Put, &w);
		if (err == MAP_OK && !Flush(&w))
			err = MAP_ERR_WRITE;
	}
	if (w.buf)
		MemPtrFree(w.buf);
	if (w.db)
		DmCloseDatabase(w.db);
	if (err == MAP_OK) {
		old = DmFindDatabase(0, (Char *)name);
		if (old) {
			Delete(kOldName);
			if (DmSetDatabaseInfo(0, old, kOldName, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
			    NULL, NULL, NULL) != errNone) {
				old = 0;
				err = MAP_ERR_WRITE;
			}
		}
		if (err == MAP_OK && DmSetDatabaseInfo(0, id, (Char *)name, &attr, &version, NULL,
		    NULL, NULL, NULL, NULL, NULL, &type, NULL) == errNone) {
			if (old)
				DmDeleteDatabase(0, old);
			return true;
		}
		if (old)
			DmSetDatabaseInfo(0, old, (Char *)name, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
				NULL, NULL, NULL);
	}
	DmDeleteDatabase(0, id);
	return false;
}

// Asks for the name to save under, starting from suggest. False on Cancel.
static Boolean SEC_EDITOR AskName(const Char *suggest, Char *name)
{
	FormType *old = FrmGetActiveForm(), *frm = FrmInitForm(EditNameForm);
	FieldType *fld;
	MemHandle h;
	Char *p;
	UInt16 i, n;
	Boolean ok = false;

	if (!frm)
		return false;
	fld = (FieldType *)Obj(frm, EditNameField);
	h = MemHandleNew(kMapNameLen);
	if (!h) {
		FrmDeleteForm(frm);
		return false;
	}
	StrCopy((Char *)MemHandleLock(h), suggest);
	MemHandleUnlock(h);
	FldSetTextHandle(fld, h);
	FldSetSelection(fld, 0, StrLen(suggest));
	FrmSetFocus(frm, FrmGetObjectIndex(frm, EditNameField));
	ViewInputTrigger();
	if (FrmDoDialog(frm) == EditNameOK) {
		p = FldGetTextPtr(fld);
		// Without spaces at either end.
		while (p && *p == ' ')
			p++;
		StrNCopy(name, p ? p : "", kMapNameLen - 1);
		name[kMapNameLen - 1] = 0;
		for (n = StrLen(name); n > 0 && name[n - 1] == ' '; n--)
			name[n - 1] = 0;
		for (i = 0; name[i]; i++)
			if ((UInt8)name[i] < 32)
				name[i] = ' ';
		ok = true;
	}
	h = FldGetTextHandle(fld);
	FldSetTextHandle(fld, NULL);
	if (h)
		MemHandleFree(h);
	FrmDeleteForm(frm);
	if (old)
		FrmSetActiveForm(old);
	return ok;
}

// The save checks. On a problem, shows its square and says what it is.
static Boolean SEC_EDITOR Check(void)
{
	GameState *g = AppGame();
	uint8_t x, y, dx, dy, cx, cy;
	int err = map_check(&g->map, &x, &y);

	if (err == MAPCHK_OK)
		err = map_settle(&gEd->undo, &g->map, &x, &y, &dx, &dy);
	if (err == MAPCHK_OK) {
		// Centred to load where it is: the view moves with it.
		if (dx || dy) {
			gEd->dirty = true;
			ViewEditWhere(&cx, &cy);
			ViewEditCentre((UInt8)(cx + dx), (UInt8)(cy + dy));
			if (gEd->selKind != ITEM_NONE)
				Select(gEd->selKind, gEd->selIndex);
			ViewEditFrame(true);
		}
		return true;
	}
	// Show the square at fault.
	if (x != 255 || y != 255) {
		UInt8 kind, index;

		gEd->zoom = false;
		ViewEditZoom(false);
		// Choose an item there, ready to drag.
		if (map_item_at(&g->map, x, y, &kind, &index)) {
			gEd->tool = TOOL_PAINT;
			gEd->choice = (UInt8)(MAP_EDIT_ITEM0 + kind);
			Select(kind, index);
		}
		Bar();
		ViewEditCentre(x, y);
		ViewEditFrame(true);
	}
	FrmCustomAlert(EditCheckAlert, map_check_text(err), "", "");
	return false;
}

// The checks, a name, then the database. True when saved.
static Boolean SEC_EDITOR Save(void)
{
	Char name[kMapNameLen], suggest[kMapNameLen];
	Boolean saved = false;
	LocalID id;

	if (!Check())
		return false;
	id = gEd->index ? DmFindDatabase(0, gEd->name) : 0;
	if (gEd->fresh) {
		StrCopy(suggest, gEd->name);
	} else if (id && Mine(id)) {
		StrCopy(suggest, gEd->name);
	} else {
		StrNCopy(suggest, gEd->name, kMapNameLen - 6);
		suggest[kMapNameLen - 6] = 0;
		StrCat(suggest, " copy");
	}
	while (AskName(suggest, name)) {
		StrCopy(suggest, name);
		if (!name[0]) {
			FrmCustomAlert(EditCheckAlert, "Enter a name.", "", "");
			continue;
		}
		id = DmFindDatabase(0, name);
		if (Reserved(name) || (id && !Mine(id))) {
			FrmCustomAlert(EditCheckAlert, "That name is taken.", "", "");
			continue;
		}
		if (id && FrmCustomAlert(EditReplaceAlert, name, "", "") != 0)
			continue;
		if (!Write(name)) {
			FrmCustomAlert(EditCheckAlert, "Not enough memory to save the map.", "", "");
			break;
		}
		gEd->dirty = false;
		gEd->fresh = 0;
		gEd->index = 1;
		StrCopy(gEd->name, name);
		// The map list comes back with it chosen.
		StrCopy(AppPrefsGet()->map, name);
		saved = true;
		break;
	}
	// The name may have opened the input area.
	ViewFormInputArea(true);
	ViewResized();
	ViewEditFrame(true);
	return saved;
}

static void SEC_EDITOR Leave(void)
{
	if (gEd->dirty) {
		UInt16 a = FrmAlert(EditLeaveAlert);

		if (a == 2 || (a == 0 && !Save()))
			return;
	}
	// A new map not saved yet leaves the list as it was.
	if (!gEd->fresh)
		MapListPick(gEd->name);
	FrmGotoForm(MapListForm);
}

static void SEC_EDITOR PickTerrain(void)
{
	FormType *frm = FrmGetActiveForm();
	ListType *lst = (ListType *)Obj(frm, EditTerrainList);
	Int16 sel;

	LstSetSelection(lst, gEd->choice);
	LstMakeItemVisible(lst, gEd->choice);
	sel = LstPopupList(lst);
	if (sel >= 0 && sel < MAP_EDIT_CHOICES)
		gEd->choice = (UInt8)sel;
	gEd->tool = gEd->last;
	if (!ItemChoice())
		Select(ITEM_NONE, 0);
	Bar();
	ViewEditFrame(false);
}

// The tool list: Paint with each brush, Fill and Box.
static void SEC_EDITOR PickTool(void)
{
	ListType *lst = (ListType *)Obj(FrmGetActiveForm(), EditToolList);
	Int16 sel = gEd->last == TOOL_FILL ? 3 : gEd->last == TOOL_BOX ? 4 : gEd->brush / 2;

	LstSetSelection(lst, sel);
	sel = LstPopupList(lst);
	if (sel >= 0 && sel <= 2) {
		gEd->last = TOOL_PAINT;
		gEd->brush = (UInt8)(sel * 2 + 1);
	} else if (sel == 3) {
		gEd->last = TOOL_FILL;
	} else if (sel == 4) {
		gEd->last = TOOL_BOX;
	}
	gEd->tool = gEd->last;
}

// Zoomed out to the whole map, or back.
static void SEC_EDITOR Zoom(Boolean on)
{
	gEd->zoom = on;
	ViewEditZoom(on);
	Bar();
	ViewEditFrame(true);
}

// Big changes ask first when there's no room to undo them.
static Boolean SEC_EDITOR MayGo(void)
{
	return map_snap_alloc() == 0 || FrmAlert(EditNoUndoAlert) == 0;
}

static void SEC_EDITOR FillAt(UInt8 x, UInt8 y)
{
	if (!MayGo())
		return;
	if (map_fill(&gEd->undo, &AppGame()->map, x, y, map_edit_terrain(gEd->choice)))
		gEd->dirty = true;
	ViewEditFrame(true);
}

// Follows the pen from square x0, y0 with a box framed on the map, and
// paints the box when the pen goes up.
static void SEC_EDITOR BoxTrack(UInt8 x0, UInt8 y0)
{
	UInt8 x1 = x0, y1 = y0, mx, my;
	Int16 x, y;
	Boolean down;
	UInt32 area;

	ViewEditBox(x0, y0, x1, y1);
	for (;;) {
		EvtGetPen(&x, &y, &down);
		if (!down)
			break;
		if (x < 0 || y < 0 || ViewTap(x, y, &mx, &my) != -1 || (mx == x1 && my == y1)) {
			SysTaskDelay(1);
			continue;
		}
		ViewEditBox(x0, y0, x1, y1);
		x1 = mx;
		y1 = my;
		ViewEditBox(x0, y0, x1, y1);
	}
	ViewEditBox(x0, y0, x1, y1);
	area = (UInt32)(x1 > x0 ? x1 - x0 + 1 : x0 - x1 + 1) * (y1 > y0 ? y1 - y0 + 1 : y0 - y1 + 1);
	if (area > MAP_UNDO_SQUARES && !MayGo())
		return;
	if (map_box(&gEd->undo, &AppGame()->map, x0, y0, x1, y1, map_edit_terrain(gEd->choice)))
		gEd->dirty = true;
	ViewEditFrame(true);
}

// Runs the map with bots; Open puts it back after.
static void SEC_EDITOR TestPlay(void)
{
	if (!Check())
		return;
	ViewEditWhere(&gEd->cx, &gEd->cy);
	if (!DraftKeep(gEd->cx, gEd->cy)) {
		Delete(kDraftName);
		Say("Not enough memory to test play.");
		return;
	}
	if (gEd->zoom) {
		gEd->zoom = false;
		ViewEditZoom(false);
	}
	Select(ITEM_NONE, 0);
	gEd->testing = gEd->draft = true;
	AppTestPlay();
}

// ---- undo ----

static void SEC_EDITOR Undo(void)
{
	MapUndo *u = &gEd->undo;
	Boolean over = u->on && u->over;

	if (!map_undo(u, &AppGame()->map)) {
		Say(over ? "That stroke was too long to undo." : "Nothing to undo.");
		return;
	}
	gEd->dirty = true;
	// Items may have moved, gone or come back.
	Select(ITEM_NONE, 0);
	ViewEditFrame(true);
}

// ---- item settings ----

static void SEC_EDITOR SetNum(FormType *frm, UInt16 id, UInt8 v)
{
	MemHandle h = MemHandleNew(4);

	if (!h)
		return;
	StrIToA((Char *)MemHandleLock(h), v);
	MemHandleUnlock(h);
	FldSetTextHandle((FieldType *)Obj(frm, id), h);
}

// The field's number up to max, or old if it's empty. Frees its text.
static UInt8 SEC_EDITOR GetNum(FormType *frm, UInt16 id, UInt8 max, UInt8 old)
{
	FieldType *fld = (FieldType *)Obj(frm, id);
	Char *p = FldGetTextPtr(fld);
	Int32 v = old;
	MemHandle h;

	if (p && *p) {
		v = StrAToI(p);
		if (v > max)
			v = max;
		if (v < 0)
			v = 0;
	}
	h = FldGetTextHandle(fld);
	FldSetTextHandle(fld, NULL);
	if (h)
		MemHandleFree(h);
	return (UInt8)v;
}

static void SEC_EDITOR Hide(FormType *frm, UInt16 id)
{
	FrmHideObject(frm, FrmGetObjectIndex(frm, id));
}

static void SEC_EDITOR SetPopup(FormType *frm, UInt16 trigger, UInt16 list, Int16 sel)
{
	ListType *lst = (ListType *)Obj(frm, list);

	LstSetSelection(lst, sel);
	CtlSetLabel((ControlType *)Obj(frm, trigger), LstGetSelectionText(lst, sel));
}

// The chosen item's owner and stock, or a start's facing. A change is a
// new undo step, or with step false part of the pen's.
static void SEC_EDITOR Settings(Boolean step)
{
	MapInfo *m = &AppGame()->map;
	UInt8 kind = gEd->selKind, i = gEd->selIndex, x, y;
	UInt8 owner = MAP_NEUTRAL, armour = 0, shells = 0, mines = 0, dir = 0, max = 0;
	FormType *old = FrmGetActiveForm(), *frm;
	Char title[8];
	Boolean ok;

	if (kind == ITEM_NONE) {
		Say("Tap a pill, base or start first.");
		return;
	}
	frm = FrmInitForm(EditItemForm);
	if (!frm)
		return;
	if (kind == ITEM_PILL) {
		owner = m->pills[i].owner;
		armour = m->pills[i].armour;
		max = R_PILL_MAX_ARMOUR;
		StrCopy(title, "Pill");
	} else if (kind == ITEM_BASE) {
		owner = m->bases[i].owner;
		armour = m->bases[i].armour;
		shells = m->bases[i].shells;
		mines = m->bases[i].mines;
		max = R_BASE_FULL_ARMOUR;
		StrCopy(title, "Base");
	} else {
		dir = m->starts[i].dir;
		StrCopy(title, "Start");
	}
	FrmSetTitle(frm, title);
	if (kind == ITEM_START) {
		Hide(frm, EditOwnerLabel);
		Hide(frm, EditOwnerTrigger);
		Hide(frm, EditArmourLabel);
		Hide(frm, EditArmourField);
		SetPopup(frm, EditFacingTrigger, EditFacingList, (4 - dir) & 15);
	} else {
		Hide(frm, EditFacingLabel);
		Hide(frm, EditFacingTrigger);
		SetPopup(frm, EditOwnerTrigger, EditOwnerList, owner > 15 ? 0 : owner + 1);
		SetNum(frm, EditArmourField, armour);
	}
	if (kind == ITEM_BASE) {
		SetNum(frm, EditShellsField, shells);
		SetNum(frm, EditMinesField, mines);
	} else {
		Hide(frm, EditShellsLabel);
		Hide(frm, EditShellsField);
		Hide(frm, EditMinesLabel);
		Hide(frm, EditMinesField);
	}
	ViewInputTrigger();
	ok = FrmDoDialog(frm) == EditItemOK;
	if (kind == ITEM_START) {
		dir = (UInt8)((4 - LstGetSelection((ListType *)Obj(frm, EditFacingList))) & 15);
	} else {
		Int16 sel = LstGetSelection((ListType *)Obj(frm, EditOwnerList));

		owner = sel <= 0 ? MAP_NEUTRAL : (UInt8)(sel - 1);
		armour = GetNum(frm, EditArmourField, max, armour);
	}
	if (kind == ITEM_BASE) {
		shells = GetNum(frm, EditShellsField, R_BASE_FULL_SHELLS, shells);
		mines = GetNum(frm, EditMinesField, R_BASE_FULL_MINES, mines);
	}
	FrmDeleteForm(frm);
	if (old)
		FrmSetActiveForm(old);
	if (ok) {
		if (step)
			map_undo_next(&gEd->undo);
		if (kind == ITEM_PILL && (owner != m->pills[i].owner || armour != m->pills[i].armour)) {
			map_undo_change(&gEd->undo, m);
			m->pills[i].owner = owner;
			m->pills[i].armour = armour;
			gEd->dirty = true;
		} else if (kind == ITEM_BASE && (owner != m->bases[i].owner ||
		    armour != m->bases[i].armour || shells != m->bases[i].shells ||
		    mines != m->bases[i].mines)) {
			map_undo_change(&gEd->undo, m);
			m->bases[i].owner = owner;
			m->bases[i].armour = armour;
			m->bases[i].shells = shells;
			m->bases[i].mines = mines;
			gEd->dirty = true;
		} else if (kind == ITEM_START && dir != m->starts[i].dir) {
			map_undo_change(&gEd->undo, m);
			m->starts[i].dir = dir;
			gEd->dirty = true;
		}
		map_item_xy(m, kind, i, &x, &y);
		ViewEditMark(x, y);
	}
	// The fields may have opened the input area.
	ViewFormInputArea(true);
	ViewResized();
	ViewEditFrame(true);
}

// ---- placing and moving items ----

// Follows the pen after it went down on the chosen item at mx, my, moving
// the item square by square to where it may stand. The ground under it is
// set on the drop. Holding still opens the settings.
static void SEC_EDITOR ItemTrack(UInt8 mx, UInt8 my, Boolean added)
{
	MapInfo *m = &AppGame()->map;
	UInt8 kind = gEd->selKind, index = gEd->selIndex, sx, sy, ix, iy, px = mx, py = my;
	UInt32 t0 = TimGetTicks(), hold = SysTicksPerSecond() * kHoldTenths / 10;
	Boolean down, moved = added, left = false;
	Int16 x, y;
	int bad = MAPITEM_OK;

	for (;;) {
		EvtGetPen(&x, &y, &down);
		if (!down)
			break;
		if (x < 0 || y < 0 || ViewTap(x, y, &sx, &sy) != -1 || (sx == px && sy == py)) {
			if (!left && TimGetTicks() - t0 >= hold) {
				if (moved) {
					map_item_ground(&gEd->undo, m, kind, index);
					ViewEditFrame(false);
				}
				Settings(false);
				return;
			}
			SysTaskDelay(1);
			continue;
		}
		px = sx;
		py = sy;
		left = true;
		map_item_xy(m, kind, index, &ix, &iy);
		bad = map_item_move(&gEd->undo, m, kind, index, sx, sy);
		if (bad == MAPITEM_OK && (ix != sx || iy != sy)) {
			ViewEditMark(ix, iy);
			ViewEditMark(sx, sy);
			Select(kind, index);
			moved = true;
			gEd->dirty = true;
			ViewEditFrame(false);
		}
	}
	if (moved) {
		map_item_xy(m, kind, index, &ix, &iy);
		map_item_ground(&gEd->undo, m, kind, index);
		ViewEditMark(ix, iy);
		ViewEditFrame(false);
	}
	map_item_xy(m, kind, index, &ix, &iy);
	if (bad != MAPITEM_OK && (px != ix || py != iy))
		Say(map_item_text(bad, kind));
}

// A pen down on square mx, my with an item or Erase item chosen.
static void SEC_EDITOR ItemPen(UInt8 mx, UInt8 my)
{
	MapInfo *m = &AppGame()->map;
	UInt8 kind, index;
	int err;

	if (map_item_at(m, mx, my, &kind, &index)) {
		if (gEd->choice == MAP_EDIT_REMOVE) {
			map_item_remove(&gEd->undo, m, kind, index);
			Select(ITEM_NONE, 0);
			ViewEditMark(mx, my);
			gEd->dirty = true;
			ViewEditFrame(false);
			return;
		}
		Select(kind, index);
		ViewEditFrame(false);
		ItemTrack(mx, my, false);
		return;
	}
	if (gEd->choice == MAP_EDIT_REMOVE)
		return;
	kind = (UInt8)(gEd->choice - MAP_EDIT_ITEM0);
	err = map_item_add(&gEd->undo, m, kind, mx, my, &index);
	if (err != MAPITEM_OK) {
		Say(map_item_text(err, kind));
		return;
	}
	gEd->dirty = true;
	Select(kind, index);
	ViewEditMark(mx, my);
	ViewEditFrame(false);
	ItemTrack(mx, my, true);
}

static void SEC_EDITOR Button(Int16 b)
{
	switch (b) {
	case BTN_PAN:
		gEd->tool = TOOL_PAN;
		Bar();
		ViewEditFrame(false);
		break;
	case BTN_TOOL:
		// Tapped again with a terrain chosen: the tool list.
		if (gEd->tool == TOOL_PAN || gEd->choice >= MAP_EDIT_ITEM0)
			gEd->tool = gEd->last;
		else
			PickTool();
		Bar();
		ViewEditFrame(false);
		break;
	case BTN_TERRAIN:
		PickTerrain();
		break;
	case BTN_ZOOM:
		Zoom(!gEd->zoom);
		break;
	case BTN_SAVE:
		Save();
		break;
	case BTN_DONE:
		Leave();
		break;
	}
}

// Follows the pen from a pen down on the map until it goes up, panning or
// painting as it moves. The pen is read directly, as some devices send no
// pen moves while the app is drawing.
static void SEC_EDITOR Track(void)
{
	Int16 x, y;
	Boolean down;
	UInt8 mx, my;

	for (;;) {
		EvtGetPen(&x, &y, &down);
		if (!down)
			break;
		if ((x == gEd->px && y == gEd->py) || x < 0 || y < 0) {
			SysTaskDelay(1);
			continue;
		}
		if (gEd->tool == TOOL_PAN) {
			ViewEditScroll(gEd->px - x, gEd->py - y);
			ViewEditFrame(false);
		} else if (ViewTap(x, y, &mx, &my) == -1 && (mx != gEd->lx || my != gEd->ly)) {
			PaintTo(mx, my);
			ViewEditFrame(false);
		}
		gEd->px = x;
		gEd->py = y;
	}
}

// A pen down inside the form: the buttons, or a stroke on the map.
static Boolean SEC_EDITOR Pen(const EventType *e)
{
	RectangleType bounds;
	UInt8 mx = 0, my = 0;
	Int16 b;

	FrmGetFormBounds(FrmGetActiveForm(), &bounds);
	// The TX status bar sends taps outside the form.
	if (e->screenX < 0 || e->screenY < 0 || e->screenX >= bounds.extent.x ||
	    e->screenY >= bounds.extent.y)
		return false;
	b = ViewTap(e->screenX, e->screenY, &mx, &my);
	if (b >= 0 && b < BTN_COUNT) {
		Button(b);
		return true;
	}
	if (b != -1)
		return true;
	gEd->px = e->screenX;
	gEd->py = e->screenY;
	// Zoomed out, Pan and the items go back to the map there.
	if (gEd->zoom && (gEd->tool == TOOL_PAN || gEd->choice >= MAP_EDIT_ITEM0)) {
		gEd->zoom = false;
		ViewEditZoom(false);
		ViewEditCentre(mx, my);
		Bar();
		ViewEditFrame(true);
		return true;
	}
	map_undo_next(&gEd->undo);
	if (gEd->tool != TOOL_PAN && gEd->choice >= MAP_EDIT_ITEM0) {
		ItemPen(mx, my);
		return true;
	}
	if (gEd->tool == TOOL_FILL) {
		FillAt(mx, my);
		return true;
	}
	if (gEd->tool == TOOL_BOX) {
		BoxTrack(mx, my);
		return true;
	}
	if (gEd->tool == TOOL_PAINT) {
		gEd->lx = mx;
		gEd->ly = my;
		PaintTo(mx, my);
		ViewEditFrame(false);
	}
	Track();
	return true;
}

Boolean SEC_EDITOR EditKey(const EventType *e)
{
	WChar c = e->data.keyDown.chr;
	UInt16 k = e->data.keyDown.keyCode;
	Int16 dx = 0, dy = 0;

	if (!gEd || !gEd->open || e->eType != keyDownEvent)
		return false;
	switch (c) {
	case vchrPageUp: case vchrRockerUp: case chrUpArrow:
		dy = -1;
		break;
	case vchrPageDown: case vchrRockerDown: case chrDownArrow:
		dy = 1;
		break;
	case vchrHard1: case vchrHard2: case vchrRockerLeft: case chrLeftArrow:
		dx = -1;
		break;
	case vchrHard3: case vchrHard4: case vchrRockerRight: case chrRightArrow:
		dx = 1;
		break;
	case vchrNavChange:
		// Only presses: the change bits sit above the held bits.
		k &= (UInt16)(k >> kNavChangeShift);
		dy = (k & kNavUp) ? -1 : (k & kNavDown) ? 1 : 0;
		dx = (k & kNavLeft) ? -1 : (k & kNavRight) ? 1 : 0;
		if (!dx && !dy)
			return false;
		break;
	default:
		return false;
	}
	// Zoomed out, the whole map shows already.
	if (!gEd->zoom) {
		ViewEditScroll(dx * kKeyStep, dy * kKeyStep);
		ViewEditFrame(false);
	}
	return true;
}

static Boolean SEC_EDITOR Handler(EventType *e)
{
	switch (e->eType) {
	case frmOpenEvent:
		Open();
		return true;
	case frmCloseEvent:
		Close();
		return false;
	case frmUpdateEvent:
		if (gEd && gEd->open)
			ViewEditFrame(true);
		return true;
	case winDisplayChangedEvent:
		if (gEd && gEd->open) {
			ViewResized();
			ViewEditFrame(true);
		}
		return true;
	case penDownEvent:
		return gEd && gEd->open && Pen(e);
	case menuEvent:
		if (!gEd || !gEd->open)
			return true;
		if (e->data.menu.itemID == EditSaveItem)
			Save();
		else if (e->data.menu.itemID == EditLeaveItem)
			Leave();
		else if (e->data.menu.itemID == EditUndoItem)
			Undo();
		else if (e->data.menu.itemID == EditItemItem)
			Settings(true);
		else if (e->data.menu.itemID == EditZoomItem)
			Zoom(!gEd->zoom);
		else if (e->data.menu.itemID == EditTestItem)
			TestPlay();
		return true;
	default:
		break;
	}
	return false;
}

FormEventHandlerType *SEC_EDITOR EditFormHandler(void)
{
	return Handler;
}
