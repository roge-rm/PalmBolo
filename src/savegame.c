#include "palmbolo.h"
#include "game/game.h"
#include "game/mapstore.h"

static Boolean gSaveAllowed = true;

void SEC_FRONT SaveAllow(Boolean allow)
{
	gSaveAllowed = allow;
}

Boolean SEC_FRONT SaveAllowed(void)
{
	return gSaveAllowed;
}

#define kSaveDbName     "PalmBoloSavedGame"
#define kSaveDbType     'Sgam'
#define kSaveCreator    'Rmrf'
#define kSaveMagic      0x50426F53UL    // "PBoS"
#define kSaveVersion    2

typedef struct SaveHead {
	UInt32 magic;
	UInt16 version;
	UInt16 size;        // sizeof(GameState)
} SaveHead;

#define kTmpDbName      "PalmBoloSavingGame"
#define kTmpDbType      'Stmp'

static void SEC_FRONT DeleteTmpDb(void)
{
	LocalID id = DmFindDatabase(0, kTmpDbName);

	if (id)
		DmDeleteDatabase(0, id);
}

static void SEC_FRONT DeleteStateDb(void)
{
	LocalID id = DmFindDatabase(0, kSaveDbName);

	if (id)
		DmDeleteDatabase(0, id);
}

// Opens the state database, or NULL when it is missing or from another build.
static DmOpenRef SEC_FRONT OpenState(void)
{
	DmOpenRef db = DmOpenDatabaseByTypeCreator(kSaveDbType, kSaveCreator, dmModeReadOnly);
	MemHandle h;

	if (!db)
		return NULL;
	h = DmQueryRecord(db, 0);
	if (DmNumRecords(db) == 1 && h && (MemHandleSize(h) == sizeof(SaveHead) + sizeof(GameState) ||
	    MemHandleSize(h) == sizeof(SaveHead) + sizeof(GameState) + 1)) {
		const SaveHead *sh = (const SaveHead *)MemHandleLock(h);
		Boolean ok = sh->magic == kSaveMagic && sh->size == sizeof(GameState) &&
			((sh->version == 1 && MemHandleSize(h) == sizeof(SaveHead) + sizeof(GameState)) ||
			 (sh->version == kSaveVersion && MemHandleSize(h) == sizeof(SaveHead) + sizeof(GameState) + 1));

		MemHandleUnlock(h);
		if (ok)
			return db;
	}
	DmCloseDatabase(db);
	return NULL;
}

Boolean SEC_FRONT SaveExists(void)
{
	DmOpenRef db;

	if (!mapstore_have_save())
		return false;
	db = OpenState();
	if (!db)
		return false;
	DmCloseDatabase(db);
	return true;
}

// The state is written to a database of another type first, then the map is
// saved, then the state replaces the old one. State allocation failure
// leaves the live game and the old save as they were.
Boolean SEC_FRONT SaveWrite(const GameState *g)
{
	DmOpenRef db;
	MemHandle h;
	LocalID id;
	UInt16 index = dmMaxRecordIndex;
	UInt32 type = kSaveDbType;
	SaveHead sh;
	UInt8 *rec;
	UInt8 viewer = game_viewer();

	if (!gSaveAllowed) return false;
	DeleteTmpDb();
	if (DmCreateDatabase(0, kTmpDbName, kSaveCreator, kTmpDbType, false) != errNone)
		return false;
	db = DmOpenDatabaseByTypeCreator(kTmpDbType, kSaveCreator, dmModeReadWrite);
	if (!db) {
		DeleteTmpDb();
		return false;
	}
	h = DmNewRecord(db, &index, sizeof(sh) + sizeof(GameState) + 1);
	if (!h) {
		DmCloseDatabase(db);
		DeleteTmpDb();
		return false;
	}
	sh.magic = kSaveMagic;
	sh.version = kSaveVersion;
	sh.size = sizeof(GameState);
	rec = (UInt8 *)MemHandleLock(h);
	DmWrite(rec, 0, &sh, sizeof(sh));
	DmWrite(rec, sizeof(sh), g, sizeof(GameState));
	DmWrite(rec, sizeof(sh) + sizeof(GameState), &viewer, 1);
	MemHandleUnlock(h);
	DmReleaseRecord(db, index, true);
	DmCloseDatabase(db);
	if (mapstore_save() != 0) {
		DeleteTmpDb();
		return false;
	}
	DeleteStateDb();
	id = DmFindDatabase(0, kTmpDbName);
	if (!id || DmSetDatabaseInfo(0, id, kSaveDbName, NULL, NULL, NULL, NULL, NULL, NULL,
	        NULL, NULL, &type, NULL) != errNone) {
		// Without its state the saved map is of no use.
		DeleteTmpDb();
		mapstore_drop_save();
		return false;
	}
	return true;
}

Boolean SEC_FRONT SaveRead(GameState *g)
{
	DmOpenRef db = OpenState();
	MemHandle h;

	if (!db)
		return false;
	h = DmQueryRecord(db, 0);
	{
		const SaveHead *sh = (const SaveHead *)MemHandleLock(h);
		const UInt8 *state = (const UInt8 *)sh + sizeof(SaveHead);
		UInt8 viewer = sh->version == 1 ? 0 : state[sizeof(GameState)];

		MemMove(g, state, sizeof(GameState));
		if (viewer >= GAME_MAX_PLAYERS || game_tank_of(g, viewer) < 0) {
			MemHandleUnlock(h);
			DmCloseDatabase(db);
			return false;
		}
		game_set_viewer(viewer);
	}
	MemHandleUnlock(h);
	DmCloseDatabase(db);
	return mapstore_resume() == 0;
}

void SEC_FRONT SaveDrop(void)
{
	if (!gSaveAllowed) return;
	DeleteTmpDb();
	DeleteStateDb();
	mapstore_drop_save();
}

