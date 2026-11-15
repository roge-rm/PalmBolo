// Finding maps and loading one into the game: the built-in Everard Island
// resource, or a database of type Map made by tools/map2pdb.py.
#include "palmbolo.h"
#include "resource.h"
#include "game/game.h"
#include "game/mapstore.h"
#include "game/util.h"
#include "game/mapthumb.h"

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

Boolean SEC_COLD MapPreview(UInt16 index, const Char *name, UInt8 *pixels)
{
	static MapSource ms;
	int err;

	if (index == 0) {
		MemHandle h = DmGetResource(kMapResType, EverardMapRes);
		MapMemory mm;

		if (!h) return false;
		map_source_memory(&ms, &mm, MemHandleLock(h), MemHandleSize(h));
		err = map_thumb(&ms, pixels);
		MemHandleUnlock(h);
		DmReleaseResource(h);
	} else {
		DbReader r;
		LocalID id = DmFindDatabase(0, (Char *)name);

		if (!id) return false;
		MemSet(&r, sizeof(r), 0);
		r.db = DmOpenDatabase(0, id, dmModeReadOnly);
		if (!r.db) return false;
		r.numRecs = DmNumRecords(r.db);
		ms.fill = DbFill; ms.ctx = &r; ms.pos = ms.n = ms.eof = 0;
		err = map_thumb(&ms, pixels);
		DbRelease(&r);
		DmCloseDatabase(r.db);
	}
	return err == MAP_OK;
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

// ---- maps for network play ----
// A map is known by the CRC-32 of its file bytes and their length.

#define kRecBytes       8000    // record size of a received map, as tools/map2pdb.py
#define kInName         "PalmBoloMapIn"
#define kInType         'Mtmp'

static UInt32 SEC_COLD CrcBytes(UInt32 crc, const UInt8 *p, UInt32 n)
{
	while (n > 0) {
		UInt16 k = n > 0x8000 ? 0x8000 : (UInt16)n;

		crc = util_crc(crc, p, k);
		p += k;
		n -= k;
	}
	return crc;
}

// The CRC and length of a database's records, end to end.
static Boolean SEC_COLD DbSum(LocalID id, UInt32 wantLen, UInt32 *sum, UInt32 *len)
{
	DmOpenRef db = DmOpenDatabase(0, id, dmModeReadOnly);
	UInt16 i, n;
	UInt32 total = 0, crc = 0xFFFFFFFFUL;

	if (!db)
		return false;
	n = DmNumRecords(db);
	for (i = 0; i < n; i++) {
		MemHandle h = DmQueryRecord(db, i);

		if (h)
			total += MemHandleSize(h);
	}
	*len = total;
	if (wantLen && total != wantLen) {
		DmCloseDatabase(db);
		return false;
	}
	for (i = 0; i < n; i++) {
		MemHandle h = DmQueryRecord(db, i);

		if (!h)
			continue;
		crc = CrcBytes(crc, (const UInt8 *)MemHandleLock(h), MemHandleSize(h));
		MemHandleUnlock(h);
	}
	DmCloseDatabase(db);
	*sum = ~crc;
	return true;
}

static Boolean SEC_COLD EverardSum(UInt32 *sum, UInt32 *len)
{
	static UInt32 sSum, sLen;
	MemHandle h;

	if (!sLen) {
		h = DmGetResource(kMapResType, EverardMapRes);
		if (!h)
			return false;
		sLen = MemHandleSize(h);
		sSum = ~CrcBytes(0xFFFFFFFFUL, (const UInt8 *)MemHandleLock(h), sLen);
		MemHandleUnlock(h);
		DmReleaseResource(h);
	}
	*sum = sSum;
	*len = sLen;
	return true;
}

Boolean SEC_COLD MapFileSum(UInt16 index, const Char *name, UInt32 *sum, UInt32 *len)
{
	LocalID id;

	if (index == 0)
		return EverardSum(sum, len);
	id = DmFindDatabase(0, (Char *)name);
	return id && DbSum(id, 0, sum, len);
}

Int16 SEC_COLD MapFind(UInt32 sum, UInt32 len, Char *name, Boolean dbs)
{
	UInt32 s, l;
	UInt16 card;
	LocalID id;
	DmSearchStateType state;
	Boolean first = true;

	if (EverardSum(&s, &l) && s == sum && l == len) {
		StrCopy(name, kEverardName);
		return 0;
	}
	while (dbs && DmGetNextDatabaseByTypeCreator(first, &state, kMapType, kMapCreator,
	    false, &card, &id) == errNone) {
		first = false;
		if (card == 0 && DbSum(id, len, &s, &l) && s == sum &&
		    DmDatabaseInfo(card, id, name, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
		        NULL, NULL, NULL) == errNone)
			return 1;
	}
	return -1;
}

Boolean SEC_COLD MapBytesOpen(MapBytes *m, UInt16 index, const Char *name)
{
	MemSet(m, sizeof(*m), 0);
	if (index == 0) {
		m->res = DmGetResource(kMapResType, EverardMapRes);
		return m->res != NULL;
	}
	{
		LocalID id = DmFindDatabase(0, (Char *)name);

		if (!id)
			return false;
		m->db = DmOpenDatabase(0, id, dmModeReadOnly);
	}
	return m->db != NULL;
}

void SEC_COLD MapBytesGet(void *ctx, UInt16 off, UInt8 *dst, UInt8 n)
{
	MapBytes *m = (MapBytes *)ctx;
	UInt32 at = off;
	UInt16 i, recs;

	MemSet(dst, n, 0);
	if (m->res) {
		const UInt8 *p = (const UInt8 *)MemHandleLock(m->res);

		if (at + n <= MemHandleSize(m->res))
			MemMove(dst, p + at, n);
		MemHandleUnlock(m->res);
		return;
	}
	if (!m->db)
		return;
	// The records in turn until the bytes are found; a chunk can run over
	// into the next.
	recs = DmNumRecords(m->db);
	for (i = 0; i < recs && n > 0; i++) {
		MemHandle h = DmQueryRecord(m->db, i);
		UInt32 size = h ? MemHandleSize(h) : 0, k;

		if (at >= size) {
			at -= size;
			continue;
		}
		k = size - at < n ? size - at : n;
		MemMove(dst, (const UInt8 *)MemHandleLock(h) + at, k);
		MemHandleUnlock(h);
		dst += k;
		n = (UInt8)(n - k);
		at = 0;
	}
}

void SEC_COLD MapBytesClose(MapBytes *m)
{
	if (m->res)
		DmReleaseResource(m->res);
	if (m->db)
		DmCloseDatabase(m->db);
	MemSet(m, sizeof(*m), 0);
}

static void SEC_COLD DeleteDb(const Char *name)
{
	LocalID id = DmFindDatabase(0, (Char *)name);

	if (id)
		DmDeleteDatabase(0, id);
}

Boolean SEC_COLD MapInOpen(MapIn *w, UInt32 len)
{
	MemSet(w, sizeof(*w), 0);
	DeleteDb(kInName);
	if (DmCreateDatabase(0, kInName, kMapCreator, kInType, false) != errNone)
		return false;
	w->db = DmOpenDatabaseByTypeCreator(kInType, kMapCreator, dmModeReadWrite);
	w->len = len;
	return w->db != NULL;
}

UInt8 SEC_COLD MapInPut(void *ctx, UInt16 off, const UInt8 *src, UInt8 n)
{
	MapIn *w = (MapIn *)ctx;
	UInt32 at = off;

	if (!w->db)
		return 1;
	while (n > 0) {
		UInt16 r = (UInt16)(at / kRecBytes), index = r;
		UInt32 o = at % kRecBytes, k = kRecBytes - o;
		MemHandle h;

		if (k > n)
			k = n;
		if (r >= DmNumRecords(w->db)) {
			UInt32 size = w->len - (UInt32)r * kRecBytes;

			if (size > kRecBytes)
				size = kRecBytes;
			h = DmNewRecord(w->db, &index, size);
		} else {
			h = DmGetRecord(w->db, r);
		}
		if (!h)
			return 1;
		DmWrite(MemHandleLock(h), o, src, k);
		MemHandleUnlock(h);
		DmReleaseRecord(w->db, index, true);
		src += k;
		at += k;
		n = (UInt8)(n - k);
	}
	return 0;
}

Boolean SEC_COLD MapInKeep(MapIn *w, const Char *name, Char *saved)
{
	LocalID id;
	UInt32 type = kMapType;
	UInt16 attr = dmHdrAttrBackup, i;
	Char base[kMapNameLen];

	if (!w->db)
		return false;
	DmCloseDatabase(w->db);
	w->db = NULL;
	id = DmFindDatabase(0, kInName);
	if (!id)
		return false;
	// The map's own name, or with a number after it if a database has that
	// name already.
	StrNCopy(base, name[0] ? name : "Map", kMapNameLen - 1);
	base[kMapNameLen - 1] = 0;
	StrCopy(saved, base);
	for (i = 2; DmFindDatabase(0, saved) && i < 100; i++) {
		Char num[4];

		StrPrintF(num, " %u", i);
		if (StrLen(base) + StrLen(num) > kMapNameLen - 1)
			base[kMapNameLen - 1 - StrLen(num)] = 0;
		StrCopy(saved, base);
		StrCat(saved, num);
	}
	return DmSetDatabaseInfo(0, id, saved, &attr, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
		&type, NULL) == errNone;
}

void SEC_COLD MapInDrop(MapIn *w)
{
	if (w->db)
		DmCloseDatabase(w->db);
	w->db = NULL;
	DeleteDb(kInName);
}
