// Storage-only import. No app globals, game state or cross-segment calls.
#include "mapio.h"
#include "game/mapthumb.h"

#define MAP_CREATOR 'Rmrf'
#define MAP_TYPE 'Map '
#define STAGE_TYPE 'Mimp'
#define CHUNK 4096

typedef struct {
	DmOpenRef db;
	UInt16 record;
	Boolean failed;
} ImportReader;

// Read stored records in slices that fit the parser's buffer.
typedef struct { ImportReader reader; UInt32 offset; } ValidateReader;
static UInt32 ValidateFill(void *ctx, UInt8 *dst, UInt32 max)
{
	ValidateReader *r = ctx;
	MemHandle h;
	UInt32 n;
	while (r->reader.record < DmNumRecords(r->reader.db)) {
		h = DmQueryRecord(r->reader.db, r->reader.record);
		if (!h) { r->reader.failed = true; return 0; }
		n = MemHandleSize(h) - r->offset;
		if (!n) { r->reader.record++; r->offset = 0; continue; }
		if (n > max) n = max;
		MemMove(dst, (UInt8 *)MemHandleLock(h) + r->offset, n);
		MemHandleUnlock(h);
		r->offset += n;
		return n;
	}
	return 0;
}

// Names the app keeps for its own databases.
static Boolean Reserved(const Char *n)
{
	return StrCaselessCompare(n, "Everard Island") == 0 ||
		StrCaselessCompare(n, "PalmBolo import") == 0 ||
		StrCaselessCompare(n, "PalmBoloMapCache") == 0 ||
		StrCaselessCompare(n, "PalmBoloSavedMap") == 0 ||
		StrCaselessCompare(n, "PalmBoloMapIn") == 0 ||
		StrCaselessCompare(n, "PalmBoloPeerMap") == 0 ||
		StrCaselessCompare(n, "PalmBoloSavedGame") == 0 ||
		StrCaselessCompare(n, "PalmBoloSavingGame") == 0 ||
		StrCaselessCompare(n, "PalmBoloDesync") == 0 ||
		StrCaselessCompare(n, "PalmBoloLog") == 0;
}

static Boolean UniqueName(const Char *base, Char *out)
{
	UInt16 i;
	Char suffix[8];
	StrNCopy(out, base, MAP_IO_NAME - 1); out[MAP_IO_NAME - 1] = 0;
	for (i = 2; DmFindDatabase(0, out) || Reserved(out); i++) {
		if (i == 10000) return false;
		StrPrintF(suffix, " %u", i);
		StrNCopy(out, base, MAP_IO_NAME - 1); out[MAP_IO_NAME - 1 - StrLen(suffix)] = 0;
		StrCat(out, suffix);
	}
	return true;
}

Err MapImport(MapReadProc read, void *ctx, UInt32 length, const Char *name, Char *saved)
{
	Char stage[MAP_IO_NAME], base[MAP_IO_NAME];
	DmOpenRef db = NULL;
	LocalID id = 0;
	UInt8 *buffer;
	UInt32 total = 0, n, got, type = MAP_TYPE;
	UInt16 rec, attr = dmHdrAttrBackup, i, j = 0;
	Err err = errNone;
	ValidateReader reader;
	MapSource source;
	if (length > MAP_IO_LIMIT) return exgErrBadData;
	buffer = MemPtrNew(CHUNK);
	if (!buffer) return exgMemError;
	// Remove staging databases left by an interrupted import.
	for (;;) {
		DmSearchStateType state;
		UInt16 card;
		LocalID old;
		if (DmGetNextDatabaseByTypeCreator(true, &state, STAGE_TYPE, MAP_CREATOR, false, &card, &old)) break;
		if (DmDeleteDatabase(card, old)) break;
	}
	StrCopy(stage, "PalmBolo import");
	err = DmCreateDatabase(0, stage, MAP_CREATOR, STAGE_TYPE, false);
	if (err) goto done;
	id = DmFindDatabase(0, stage);
	db = DmOpenDatabase(0, id, dmModeReadWrite);
	if (!db) { err = exgErrDeviceFull; goto done; }
	for (;;) {
		MemHandle h;
		n = 0;
		while (n < CHUNK) {
			got = read(ctx, buffer + n, CHUNK - n, &err);
			if (err) goto done;
			if (got > CHUNK - n) { err = exgErrBadData; goto done; }
			if (!got) break;
			n += got;
		}
		if (!n) break;
		if (n > CHUNK || n > MAP_IO_LIMIT - total || (length && n > length - total)) {
			err = exgErrBadData; goto done;
		}
		rec = dmMaxRecordIndex;
		h = DmNewRecord(db, &rec, n);
		if (!h) { err = exgErrDeviceFull; goto done; }
		err = DmWrite(MemHandleLock(h), 0, buffer, n);
		MemHandleUnlock(h);
		if (DmReleaseRecord(db, rec, true) && !err) err = exgErrDeviceFull;
		if (err) goto done;
		total += n;
		if (n < CHUNK) break;
	}
	if (!total || (length && total != length)) { err = exgErrBadData; goto done; }
	MemSet(&reader, sizeof(reader), 0); reader.reader.db = db;
	MemSet(&source, sizeof(source), 0); source.fill = ValidateFill; source.ctx = &reader;
	if (map_thumb(&source, NULL) != MAP_OK || reader.reader.failed) { err = exgErrBadData; goto done; }
	// Drop directory prefixes and the extension, replace control characters.
	if (!name) name = "Map";
	for (i = 0; i < 1024 && name[i]; i++) if (name[i] == '/' || name[i] == '\\') j = i + 1;
	StrNCopy(base, name + j, MAP_IO_NAME - 1); base[MAP_IO_NAME - 1] = 0;
	i = StrLen(base);
	if (i >= 4 && StrCaselessCompare(base + i - 4, ".map") == 0) base[i - 4] = 0;
	for (i = 0; base[i]; i++) if ((UInt8)base[i] < 32) base[i] = ' ';
	if (!base[0]) StrCopy(base, "Map");
	if (!UniqueName(base, saved)) { err = exgErrDeviceFull; goto done; }
	err = DmCloseDatabase(db);
	if (err) goto done;
	db = NULL;
	err = DmSetDatabaseInfo(0, id, saved, &attr, NULL, NULL, NULL, NULL, NULL, NULL, NULL, &type, NULL);
	if (!err) id = 0;
done:
	if (db) DmCloseDatabase(db);
	if (id) DmDeleteDatabase(0, id);
	MemPtrFree(buffer);
	return err;
}

static UInt32 ReceiveRead(void *ctx, UInt8 *dst, UInt32 max, Err *err)
{
	return ExgReceive((ExgSocketType *)ctx, dst, max, err);
}
Err MapReceive(ExgSocketType *socket)
{
	Char saved[MAP_IO_NAME];
	Err err = ExgAccept(socket);
	if (err) return err;
	err = MapImport(ReceiveRead, socket, socket->length, socket->name, saved);
	ExgDisconnect(socket, err);
	// Exchange has already reported any import error.
	return errNone;
}
