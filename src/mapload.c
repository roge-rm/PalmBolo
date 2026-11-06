// Finding maps and loading one into the game: the built-in Everard Island
// resource, or a database of type Map made by tools/map2pdb.py.
#include "palmbolo.h"
#include "resource.h"
#include "game/game.h"
#include "game/mapstore.h"

#define kEverardName "Everard Island"

UInt16 SEC_COLD MapListBuild(Char names[][kMapNameLen])
{
	UInt16 n = 0, card;
	LocalID id;
	DmSearchStateType state;
	Boolean first = true;

	StrCopy(names[n++], kEverardName);
	while (n < kMaxMaps &&
	    DmGetNextDatabaseByTypeCreator(first, &state, kMapType, kMapCreator,
	        false, &card, &id) == errNone) {
		first = false;
		if (DmDatabaseInfo(card, id, names[n], NULL, NULL, NULL, NULL, NULL,
		        NULL, NULL, NULL, NULL, NULL) == errNone)
			n++;
	}
	return n;
}

// Reads the file bytes from the records of a database, one record locked at
// a time.
typedef struct DbReader {
	DmOpenRef db;
	UInt16 rec, numRecs;
	MemHandle h;
	const UInt8 *p;
	UInt32 pos, size;
} DbReader;

static void SEC_COLD DbRelease(DbReader *r)
{
	if (r->h) {
		MemHandleUnlock(r->h);
		r->h = NULL;
	}
}

static uint32_t SEC_COLD DbFill(void *ctx, uint8_t *dst, uint32_t max)
{
	DbReader *r = (DbReader *)ctx;
	uint32_t n = 0, k;

	while (n < max) {
		if (!r->h || r->pos >= r->size) {
			DbRelease(r);
			if (r->rec >= r->numRecs)
				break;
			r->h = DmQueryRecord(r->db, r->rec++);
			if (!r->h)
				break;
			r->p = MemHandleLock(r->h);
			r->size = MemHandleSize(r->h);
			r->pos = 0;
			continue;
		}
		k = r->size - r->pos;
		if (k > max - n)
			k = max - n;
		MemMove(dst + n, r->p + r->pos, k);
		r->pos += k;
		n += k;
	}
	return n;
}

Int16 SEC_COLD MapLoad(UInt16 index, const Char *name, GameState *game)
{
	static MapSource ms;    // kept out of the stack
	int err;

	if (mapstore_open(TERRAIN_DEEP_SEA) != 0)
		return -1;
	if (index == 0) {
		MemHandle h = DmGetResource(kMapResType, EverardMapRes);
		MapMemory mm;

		if (!h)
			return -1;
		map_source_memory(&ms, &mm, MemHandleLock(h), MemHandleSize(h));
		err = map_parse(&game->map, &ms);
		MemHandleUnlock(h);
		DmReleaseResource(h);
	} else {
		DbReader r;
		LocalID id = DmFindDatabase(0, (Char *)name);

		if (!id)
			return -1;
		MemSet(&r, sizeof(r), 0);
		r.db = DmOpenDatabase(0, id, dmModeReadOnly);
		if (!r.db)
			return -1;
		r.numRecs = DmNumRecords(r.db);
		ms.fill = DbFill;
		ms.ctx = &r;
		ms.pos = ms.n = 0;
		ms.eof = 0;
		err = map_parse(&game->map, &ms);
		DbRelease(&r);
		DmCloseDatabase(r.db);
	}
	return err;
}
