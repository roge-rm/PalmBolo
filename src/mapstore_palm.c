// Map storage on the Palm: the 256 x 256 terrain bytes live in the database
// PalmBoloMapCache, kRows rows to a record (a record can't hold 64 KB on
// OS 3). The records stay locked while the map is open, and writes go
// through DmWrite. Nothing here is allocated in the dynamic heap.
#include "palmbolo.h"
#include "game/mapstore.h"

#define kCacheName      "PalmBoloMapCache"
#define kCacheType      'Cach'
#define kCreator        'Rmrf'
#define kRowsPerRecord  16
#define kRecords        (256 / kRowsPerRecord)
#define kRecordBytes    (kRowsPerRecord * 256)

static DmOpenRef gDb;
static UInt8 *gRec[kRecords];   // locked record pointers
static MemHandle gHandle[kRecords];

static void Unlock(void)
{
	UInt16 i;

	for (i = 0; i < kRecords; i++) {
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
static DmOpenRef OpenCache(void)
{
	DmOpenRef db;
	LocalID id;
	UInt16 i, index;
	MemHandle h;

	db = DmOpenDatabaseByTypeCreator(kCacheType, kCreator, dmModeReadWrite);
	if (db) {
		Boolean ok = DmNumRecords(db) == kRecords;

		for (i = 0; ok && i < kRecords; i++) {
			h = DmQueryRecord(db, i);
			ok = h && MemHandleSize(h) == kRecordBytes;
		}
		if (ok)
			return db;
		DmCloseDatabase(db);
		id = DmFindDatabase(0, kCacheName);
		if (id)
			DmDeleteDatabase(0, id);
	}
	if (DmCreateDatabase(0, kCacheName, kCreator, kCacheType, false) != errNone)
		return NULL;
	db = DmOpenDatabaseByTypeCreator(kCacheType, kCreator, dmModeReadWrite);
	if (!db)
		return NULL;
	for (i = 0; i < kRecords; i++) {
		index = dmMaxRecordIndex;
		h = DmNewRecord(db, &index, kRecordBytes);
		if (!h) {
			DmCloseDatabase(db);
			id = DmFindDatabase(0, kCacheName);
			if (id)
				DmDeleteDatabase(0, id);
			return NULL;
		}
		DmReleaseRecord(db, index, true);
	}
	return db;
}

int mapstore_open(uint8_t value)
{
	UInt16 i;

	mapstore_close();
	gDb = OpenCache();
	if (!gDb)
		return 1;
	for (i = 0; i < kRecords; i++) {
		gHandle[i] = DmQueryRecord(gDb, i);
		gRec[i] = gHandle[i] ? MemHandleLock(gHandle[i]) : NULL;
		if (!gRec[i]) {
			mapstore_close();
			return 1;
		}
	}
	mapstore_fill(value);
	return 0;
}

void mapstore_fill(uint8_t value)
{
	UInt16 i;

	for (i = 0; i < kRecords; i++)
		DmSet(gRec[i], 0, kRecordBytes, value);
}

const uint8_t *mapstore_row(uint8_t y)
{
	return gRec[y / kRowsPerRecord] + (UInt32)(y % kRowsPerRecord) * 256;
}

uint8_t mapstore_get(uint8_t x, uint8_t y)
{
	return mapstore_row(y)[x];
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
