// Debug log: text lines in the database PalmBoloLog, one record per line.
// Only built with DEBUG=1.
#include "palmbolo.h"

#ifdef DEBUG

#define kLogName    "PalmBoloLog"
#define kLogType    'Log '
#define kLogCreator 'Rmrf'
#define kLogLine    160

// Opens the database, creating it with the backup bit set if it is missing.
static DmOpenRef SEC_UI LogOpen(void)
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

// Appends c to the line unless it is full; the line always keeps room for
// the closing zero.
static void SEC_UI Put(Char *line, UInt16 *len, Char c)
{
	if (*len < kLogLine - 1)
		line[(*len)++] = c;
}

// A small vsprintf that never writes past the line: %d %u %x %lx %ld %lu
// %s %c and %%, with a width and a 0 flag. A long line is cut short. Int16
// and UInt16 arguments arrive as 16-bit ints, as with StrPrintF.
static void SEC_UI Format(Char *line, UInt16 *len, const Char *fmt, va_list ap)
{
	Char num[12];
	const Char *p;
	UInt16 width, n, i;
	Boolean zero, lng;
	UInt32 v;
	Char c;

	for (; *fmt; fmt++) {
		if (*fmt != '%') {
			Put(line, len, *fmt);
			continue;
		}
		fmt++;
		zero = *fmt == '0';
		if (zero)
			fmt++;
		for (width = 0; *fmt >= '0' && *fmt <= '9'; fmt++)
			width = (UInt16)(width * 10 + (*fmt - '0'));
		lng = *fmt == 'l';
		if (lng)
			fmt++;
		c = *fmt;
		if (!c)
			break;
		n = 0;
		p = num;
		switch (c) {
		case 's':
			p = va_arg(ap, const Char *);
			if (!p)
				p = "(null)";
			for (n = 0; p[n] && n < kLogLine; n++)
				;
			break;
		case 'c':
			num[0] = (Char)va_arg(ap, int);
			n = 1;
			break;
		case 'd': case 'u': case 'x': case 'X': {
			Boolean neg = false;
			UInt16 base = (c == 'x' || c == 'X') ? 16 : 10;

			if (lng)
				v = va_arg(ap, UInt32);
			else if (c == 'd')
				v = (UInt32)(Int32)(Int16)va_arg(ap, int);
			else
				v = (UInt16)va_arg(ap, int);
			if (c == 'd' && (Int32)v < 0) {
				neg = true;
				v = (UInt32)(-(Int32)v);
			}
			// Digits are made backwards at the end of num.
			i = sizeof(num);
			do {
				num[--i] = "0123456789abcdef"[v % base];
				v /= base;
			} while (v);
			if (neg)
				num[--i] = '-';
			p = num + i;
			n = (UInt16)(sizeof(num) - i);
			break;
		}
		default:
			num[0] = c;
			n = 1;
			break;
		}
		for (; width > n; width--)
			Put(line, len, zero ? '0' : ' ');
		for (i = 0; i < n; i++)
			Put(line, len, p[i]);
	}
}

static void SEC_UI Append(Char *line, UInt16 *len, const Char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	Format(line, len, fmt, ap);
	va_end(ap);
}

void SEC_UI LogF(const Char *fmt, ...)
{
	Char line[kLogLine];
	DmOpenRef db;
	MemHandle h;
	UInt16 index, len = 0;
	va_list ap;

	Append(line, &len, "%lu %lu ", TimGetSeconds(), TimGetTicks());
	va_start(ap, fmt);
	Format(line, &len, fmt, ap);
	va_end(ap);
	line[len++] = 0;

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

void SEC_UI LogClear(void)
{
	DmOpenRef db = LogOpen();

	if (!db)
		return;
	while (DmNumRecords(db) > 0)
		DmRemoveRecord(db, 0);
	DmCloseDatabase(db);
}

void SEC_UI LogDevice(const DeviceInfo *d)
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
