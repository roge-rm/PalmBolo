// Debug log: text lines in the database PalmBoloLog, one record per line.
// Only built with DEBUG=1.
#include "palmbolo.h"

#ifdef DEBUG

#define kLogName    "PalmBoloLog"
#define kLogType    'Log '
#define kLogCreator 'Rmrf'
#define kLogLine    120

// Opens the database, creating it with the backup bit set if it is missing.
static DmOpenRef LogOpen(void)
{
	DmOpenRef db = DmOpenDatabaseByTypeCreator(kLogType, kLogCreator, dmModeReadWrite);
	LocalID id;
	UInt16 attr;

	if (db)
		return db;
	if (DmCreateDatabase(0, kLogName, kLogCreator, kLogType, false) != errNone)
		return NULL;
	db = DmOpenDatabaseByTypeCreator(kLogType, kLogCreator, dmModeReadWrite);
	if (db && DmOpenDatabaseInfo(db, &id, NULL, NULL, NULL, NULL) == errNone &&
	    DmDatabaseInfo(0, id, NULL, &attr, NULL, NULL, NULL, NULL, NULL, NULL,
	        NULL, NULL, NULL) == errNone) {
		attr |= dmHdrAttrBackup;
		DmSetDatabaseInfo(0, id, NULL, &attr, NULL, NULL, NULL, NULL, NULL,
			NULL, NULL, NULL, NULL);
	}
	return db;
}

void LogF(const Char *fmt, ...)
{
	Char line[kLogLine];
	DmOpenRef db;
	MemHandle h;
	UInt16 index, len;
	va_list ap;

	StrPrintF(line, "%lu %lu ", TimGetSeconds(), TimGetTicks());
	len = StrLen(line);
	va_start(ap, fmt);
	StrVPrintF(line + len, fmt, ap);
	va_end(ap);
	len = StrLen(line) + 1;

	db = LogOpen();
	if (!db)
		return;
	while (DmNumRecords(db) >= kLogMaxRecords)
		DmRemoveRecord(db, 0);
	index = dmMaxRecordIndex;
	h = DmNewRecord(db, &index, len);
	if (h) {
		DmWrite(MemHandleLock(h), 0, line, len);
		MemHandleUnlock(h);
		DmReleaseRecord(db, index, true);
	}
	DmCloseDatabase(db);
}

void LogClear(void)
{
	DmOpenRef db = LogOpen();

	if (!db)
		return;
	while (DmNumRecords(db) > 0)
		DmRemoveRecord(db, 0);
	DmCloseDatabase(db);
}

void LogDevice(const DeviceInfo *d)
{
	LogF("rom %lx cpu %lx winver %lu tps %u", d->rom, d->cpu, d->winVersion,
		(UInt16)d->ticksPerSecond);
	LogF("heap free %lu max %lu total %lu", d->heapFree, d->heapMax, d->heapTotal);
	LogF("depths %lx color %d density %lu", d->depthMask, (Int16)d->hasColor,
		d->density);
	LogF("sony %d sonyHR %d pins %d", (Int16)d->sony, (Int16)d->sonyHR,
		(Int16)d->pins);
}

#endif
