// Map storage on the Palm: the 256 x 256 terrain bytes live in the database
// PalmBoloMapCache, kRows rows to a record (a record can't hold 64 KB on
// OS 3). The records stay locked while the map is open, and writes go
// through DmWrite. Nothing here is allocated in the dynamic heap. After the
// terrain come the mine flag planes, 8 KB a record.
#include "palmbolo.h"
#include "game/mapstore.h"

#define kCacheName      "PalmBoloMapCache"
#define kCacheType      'Cach'
#define kCreator        'Rmrf'
#define kRowsPerRecord  16
#define kRecords        (256 / kRowsPerRecord)
#define kRecordBytes    (kRowsPerRecord * 256)
#define kFlagBytes      8192
#define kAllRecords     (kRecords + MS_PLANES)

#define kSaveName       "PalmBoloSavedMap"
#define kSaveType       'Save'

static DmOpenRef gDb;
static UInt8 *gRec[kAllRecords];    // locked record pointers
static MemHandle gHandle[kAllRecords];
const uint8_t *mapstore_rows[256];

static void Unlock(void)
{
	UInt16 i;

	for (i = 0; i < kAllRecords; i++) {
		if (gRec[i]) {
			MemHandleUnlock(gHandle[i]);
			gRec[i] = NULL;
		}
	}
}

void mapstore_close(void)
{
	Unlock();
	if (gDb) {
		DmCloseDatabase(gDb);
		gDb = NULL;
	}
}

// Opens the cache database, creating it if it is missing or the wrong size.
static DmOpenRef OpenCacheAs(const Char *name, UInt32 type)
{
	DmOpenRef db;
	LocalID id;
	UInt16 i, index;
	MemHandle h;

	db = DmOpenDatabaseByTypeCreator(type, kCreator, dmModeReadWrite);
	if (db) {
		Boolean ok = DmNumRecords(db) == kAllRecords;

		for (i = 0; ok && i < kAllRecords; i++) {
			h = DmQueryRecord(db, i);
			ok = h && MemHandleSize(h) == (i < kRecords ? kRecordBytes : kFlagBytes);
		}
		if (ok)
			return db;
		DmCloseDatabase(db);
		id = DmFindDatabase(0, (Char *)name);
		if (id)
			DmDeleteDatabase(0, id);
	}
	if (DmCreateDatabase(0, name, kCreator, type, false) != errNone)
		return NULL;
	db = DmOpenDatabaseByTypeCreator(type, kCreator, dmModeReadWrite);
	if (!db)
		return NULL;
	for (i = 0; i < kAllRecords; i++) {
		index = dmMaxRecordIndex;
		h = DmNewRecord(db, &index, i < kRecords ? kRecordBytes : kFlagBytes);
		if (!h) {
			DmCloseDatabase(db);
			id = DmFindDatabase(0, (Char *)name);
			if (id)
				DmDeleteDatabase(0, id);
			return NULL;
		}
		DmReleaseRecord(db, index, true);
	}
	return db;
}

#ifdef LOOPBACK_TEST
// Which cache the store opens; mapstore_peer_swap swaps it too.
static const Char *gCacheName = kCacheName;
static UInt32 gCacheType = kCacheType;
#else
#define gCacheName kCacheName
#define gCacheType kCacheType
#endif

static DmOpenRef OpenCache(void)
{
	return OpenCacheAs(gCacheName, gCacheType);
}

static void SetRows(void)
{
	UInt16 i;

	for (i = 0; i < 256; i++)
		mapstore_rows[i] = gRec[i / kRowsPerRecord] + (i % kRowsPerRecord) * 256;
}

// Opens the cache and locks all its records. Non-zero on failure.
static int OpenLocked(void)
{
	UInt16 i;

	mapstore_close();
	gDb = OpenCache();
	if (!gDb)
		return 1;
	for (i = 0; i < kAllRecords; i++) {
		gHandle[i] = DmQueryRecord(gDb, i);
		gRec[i] = gHandle[i] ? MemHandleLock(gHandle[i]) : NULL;
		if (!gRec[i]) {
			mapstore_close();
			return 1;
		}
	}
	SetRows();
	return 0;
}

int mapstore_open(uint8_t value)
{
	if (OpenLocked() != 0)
		return 1;
	mapstore_fill(value);
	mapstore_clear_flags();
	return 0;
}

void mapstore_fill(uint8_t value)
{
	UInt16 i;

	for (i = 0; i < kRecords; i++)
		DmSet(gRec[i], 0, kRecordBytes, value);
}

void mapstore_set(uint8_t x, uint8_t y, uint8_t terrain)
{
	DmWrite(gRec[y / kRowsPerRecord], (UInt32)(y % kRowsPerRecord) * 256 + x,
		&terrain, 1);
}

void mapstore_set_bytes(uint8_t x, uint8_t y, uint16_t n, const uint8_t *src)
{
	if (x + n > 256)
		n = 256 - x;
	DmWrite(gRec[y / kRowsPerRecord], (UInt32)(y % kRowsPerRecord) * 256 + x, src, n);
}

uint8_t mapstore_flag(uint8_t plane, uint8_t x, uint8_t y)
{
	return (uint8_t)((gRec[kRecords + plane][(UInt16)y * 32 + (x >> 3)] >> (x & 7)) & 1);
}

void mapstore_set_flag(uint8_t plane, uint8_t x, uint8_t y, uint8_t on)
{
	UInt8 *rec = gRec[kRecords + plane];
	UInt16 off = (UInt16)y * 32 + (x >> 3);
	UInt8 b = rec[off];

	if (on)
		b = (UInt8)(b | (1 << (x & 7)));
	else
		b = (UInt8)(b & ~(1 << (x & 7)));
	if (b != rec[off])
		DmWrite(rec, off, &b, 1);
}

const uint8_t *mapstore_flags(uint8_t plane)
{
	return gRec[kRecords + plane];
}

void mapstore_clear_flags(void)
{
	UInt16 i;

	for (i = 0; i < MS_PLANES; i++)
		DmSet(gRec[kRecords + i], 0, kFlagBytes, 0);
}

// The saved game keeps the cache database itself: it is renamed and given
// another type, so nothing is copied and a new game makes a new cache.
static void DeleteByName(const Char *name)
{
	LocalID id = DmFindDatabase(0, (Char *)name);

	if (id)
		DmDeleteDatabase(0, id);
}

int mapstore_have_save(void)
{
	return DmFindDatabase(0, kSaveName) != 0;
}

void mapstore_drop_save(void)
{
	DeleteByName(kSaveName);
}

// Renames the live cache to the saved map, replacing the old save. If that
// fails the live cache is opened again as it was.
int mapstore_save(void)
{
	LocalID id;
	UInt32 type = kSaveType;
	Boolean ok;

	if (!gDb)
		return 1;
	mapstore_close();
	id = DmFindDatabase(0, kCacheName);
	if (!id)
		return 1;
	DeleteByName(kSaveName);
	ok = DmSetDatabaseInfo(0, id, kSaveName, NULL, NULL, NULL, NULL, NULL, NULL,
		NULL, NULL, &type, NULL) == errNone;
	if (!ok)
		OpenLocked();
	return ok ? 0 : 1;
}

// Copies the saved map into the live cache and leaves the save as it is, so
// a crash during the resumed game doesn't lose it.
int mapstore_resume(void)
{
	LocalID id = DmFindDatabase(0, kSaveName);
	DmOpenRef src;
	UInt16 i;
	Boolean ok;

	mapstore_close();
	if (!id)
		return 1;
	src = DmOpenDatabase(0, id, dmModeReadOnly);
	if (!src)
		return 1;
	ok = DmNumRecords(src) == kAllRecords;
	for (i = 0; ok && i < kAllRecords; i++) {
		MemHandle h = DmQueryRecord(src, i);

		ok = h && MemHandleSize(h) == (i < kRecords ? kRecordBytes : kFlagBytes);
	}
	if (ok && mapstore_open(0) != 0)
		ok = false;
	for (i = 0; ok && i < kAllRecords; i++) {
		MemHandle h = DmQueryRecord(src, i);
		const void *p = MemHandleLock(h);

		DmWrite(gRec[i], 0, p, i < kRecords ? kRecordBytes : kFlagBytes);
		MemHandleUnlock(h);
	}
	DmCloseDatabase(src);
	if (!ok)
		mapstore_close();
	return ok ? 0 : 1;
}

#ifdef LOOPBACK_TEST
// A second store for the loopback test's second copy of the game, in a
// database of its own. mapstore_peer_swap swaps it with the open store, so
// the calls above, mapstore_open too, work on it until swapped back.
#define kPeerName       "PalmBoloPeerMap"
#define kPeerType       'Peer'
static DmOpenRef gPeerDb;
static UInt8 *gPeerRec[kAllRecords];
static MemHandle gPeerHandle[kAllRecords];
static const Char *gPeerName = kPeerName;
static UInt32 gPeerType = kPeerType;

void mapstore_peer_swap(void)
{
	DmOpenRef db = gDb;
	const Char *name = gCacheName;
	UInt32 type = gCacheType;
	UInt16 i;

	gDb = gPeerDb;
	gPeerDb = db;
	gCacheName = gPeerName;
	gPeerName = name;
	gCacheType = gPeerType;
	gPeerType = type;
	for (i = 0; i < kAllRecords; i++) {
		UInt8 *r = gRec[i];
		MemHandle h = gHandle[i];

		gRec[i] = gPeerRec[i];
		gHandle[i] = gPeerHandle[i];
		gPeerRec[i] = r;
		gPeerHandle[i] = h;
	}
	if (gRec[0])
		SetRows();
}

void mapstore_peer_close(void)
{
	mapstore_peer_swap();
	mapstore_close();
	mapstore_peer_swap();
}
#endif
