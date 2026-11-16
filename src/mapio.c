#include "palmbolo.h"
#include "resource.h"
#include "mapio.h"
#include <VFSMgr.h>

#define PAGE_ROWS 7
#define PATH_SIZE 512
#define FILE_SIZE 256

typedef struct {
	UInt16 volume;
	UInt32 volumeIterator, iterator;
	Char path[PATH_SIZE];
	Char names[PAGE_ROWS][FILE_SIZE];
	Char labels[PAGE_ROWS][40];
	Char *choices[PAGE_ROWS];
	Boolean directory[PAGE_ROWS];
	UInt16 count;
	Boolean more;
} Browser;

static void SEC_MAPIO ShowError(Err err)
{
	if (!err || err == exgErrUserCancel) return;
	FrmCustomAlert(MapIoAlert, err == exgErrBadData ? "Not a valid map, incomplete or over 1 MB." :
		err == exgErrDeviceFull || err == exgMemError ? "Not enough storage for this map." :
		"Cannot read or send the map. Check the card or connection.", "", "");
}

void SEC_MAPIO MapRegister(void)
{
	// ExgRegisterData is present in OS 3.0; the replacement is OS 4.0.
	ExgRegisterData('Rmrf', exgRegExtensionID, "map");
	ExgRegisterData('Rmrf', exgRegTypeID, "application/x-bolo-map");
}

static UInt32 SEC_MAPIO CardRead(void *ctx, UInt8 *dst, UInt32 max, Err *err)
{
	UInt32 n = 0;
	EventType event;
	EvtGetEvent(&event, 0);
	if (event.eType == appStopEvent) {
		EvtAddEventToQueue(&event); *err = exgErrUserCancel; return 0;
	}
	if (event.eType == ctlSelectEvent && event.data.ctlSelect.controlID == MapCopyCancel) {
		*err = exgErrUserCancel; return 0;
	}
	if (event.eType != nilEvent && !SysHandleEvent(&event)) FrmHandleEvent(FrmGetActiveForm(), &event);
	*err = VFSFileRead(*(FileRef *)ctx, max, dst, &n);
	if (*err == vfsErrFileEOF) *err = errNone;
	return n;
}

static Err SEC_MAPIO ReadPage(Browser *b)
{
	FileRef dir;
	FileInfoType info;
	UInt32 iterator = vfsIteratorStart, skip = b->iterator;
	Err err;
	b->count = 0;
	err = VFSFileOpen(b->volume, b->path, vfsModeRead, &dir);
	if (err) return err;
	while (b->count < PAGE_ROWS && iterator != vfsIteratorStop) {
		Char *name = b->names[b->count];
		UInt16 len;
		MemSet(&info, sizeof(info), 0);
		info.nameP = name; info.nameBufLen = FILE_SIZE;
		err = VFSDirEntryEnumerate(dir, &iterator, &info);
		if (err == expErrEnumerationEmpty) { iterator = vfsIteratorStop; err = errNone; break; }
		if (err) break;
		name[FILE_SIZE - 1] = 0;
		if (!StrCompare(name, ".") || !StrCompare(name, "..")) continue;
		len = StrLen(name);
		if (!(info.attributes & vfsFileAttrDirectory) &&
						(len < 4 || StrCaselessCompare(name + len - 4, ".map"))) continue;
		if (skip) { skip--; continue; }
		b->directory[b->count] = (info.attributes & vfsFileAttrDirectory) != 0;
		{
			Char *label = b->labels[b->count];
			label[0] = b->directory[b->count] ? '/' : ' ';
			StrNCopy(label + 1, name, 38); label[39] = 0;
			b->choices[b->count] = label;
		}
		b->count++;
	}
	b->more = iterator != vfsIteratorStop;
	b->iterator += b->count;
	VFSFileClose(dir);
	return err;
}

void SEC_MAPIO MapBrowse(void)
{
	UInt32 version;
	Browser *b;
	FormType *frm, *old;
	ListType *list;
	UInt16 button;
	Err err;
	if (FtrGet(sysFileCVFSMgr, vfsFtrIDVersion, &version)) {
		FrmCustomAlert(MapIoAlert, "This Palm has no memory-card support.", "", ""); return;
	}
	b = MemPtrNew(sizeof(*b));
	if (!b) { ShowError(exgMemError); return; }
	MemSet(b, sizeof(*b), 0);
	StrCopy(b->path, "/");
	err = VFSVolumeEnumerate(&b->volume, &b->volumeIterator);
	if (err) { FrmCustomAlert(MapIoAlert, "No memory card found.", "", ""); MemPtrFree(b); return; }
	old = FrmGetActiveForm(); frm = FrmInitForm(MapCardForm);
	if (!frm) { MemPtrFree(b); ShowError(exgMemError); return; }
	FrmSetActiveForm(frm);
	list = FrmGetObjectPtr(frm, FrmGetObjectIndex(frm, MapCardList));
	for (;;) {
		err = ReadPage(b);
		if (err) { ShowError(err); break; }
		LstSetListChoices(list, b->choices, b->count); LstSetSelection(list, noListSelection);
		{
			Char label[25];
			UInt16 n;
			StrPrintF(label, "Card %u: ", b->volume);
			n = StrLen(label);
			StrNCopy(label + n, b->path, 24 - n); label[24] = 0;
			FrmCopyLabel(frm, MapCardPath, label);
		}
		button = FrmDoDialog(frm);
		if (!button || button == MapCardCancel) break;
		if (button == MapCardMore) {
			if (!b->more) b->iterator = vfsIteratorStart;
			continue;
		}
		b->iterator = vfsIteratorStart;
		if (button == MapCardVolume) {
			if (b->volumeIterator == vfsIteratorStop) b->volumeIterator = vfsIteratorStart;
			err = VFSVolumeEnumerate(&b->volume, &b->volumeIterator);
			if (err) { ShowError(err); break; }
			StrCopy(b->path, "/"); continue;
		}
		if (button == MapCardUp) {
			UInt16 n = StrLen(b->path);
			while (n > 1 && b->path[n - 1] != '/') n--;
			if (n > 1) n--;
			b->path[n] = 0; continue;
		}
		if (button == MapCardOpen) {
			Int16 sel = LstGetSelection(list);
			UInt16 n = StrLen(b->path);
			if (sel < 0 || sel >= b->count) continue;
			if (n + StrLen(b->names[sel]) + 2 > PATH_SIZE) {
				FrmCustomAlert(MapIoAlert, "Folder path is too long.", "", ""); continue;
			}
			if (n > 1) StrCat(b->path, "/");
			StrCat(b->path, b->names[sel]);
			if (!b->directory[sel]) {
				FileRef file;
				UInt32 size;
				Char saved[MAP_IO_NAME];
				err = VFSFileOpen(b->volume, b->path, vfsModeRead, &file);
				if (!err) {
					err = VFSFileSize(file, &size);
					if (!err) {
						FormType *progress = FrmInitForm(MapCopyForm);
						if (!progress) err = exgMemError;
						else {
							FrmSetActiveForm(progress); FrmDrawForm(progress);
							err = MapImport(CardRead, &file, size, b->names[sel], saved);
							FrmDeleteForm(progress); FrmSetActiveForm(frm);
						}
					}
					VFSFileClose(file);
				}
				ShowError(err);
				if (!err || err == exgErrUserCancel) break;
				b->path[n] = 0;
			}
		}
	}
	FrmDeleteForm(frm); FrmSetActiveForm(old); MemPtrFree(b);
}

static Err SEC_MAPIO SendBytes(ExgSocketType *socket, const UInt8 *bytes, UInt32 n)
{
	Err err = errNone;
	while (n) {
		UInt32 sent = ExgSend(socket, bytes, n > 4096 ? 4096 : n, &err);
		if (err) return err;
		if (!sent || sent > n) return exgErrDisconnected;
		bytes += sent; n -= sent;
	}
	return errNone;
}
void SEC_MAPIO MapBeam(UInt16 index, const Char *name)
{
	MapBytes map;
	ExgSocketType socket;
	UInt32 sum, length;
	Char file[MAP_IO_NAME + 5];
	Err err;
	UInt16 i;
	if (!MapFileSum(index, name, &sum, &length) || !MapBytesOpen(&map, index, name)) {
		ShowError(exgErrBadData); return;
	}
	MemSet(&socket, sizeof(socket), 0);
	StrCopy(file, name); StrCat(file, ".map");
	socket.name = file; socket.description = (Char *)name;
	socket.type = "application/x-bolo-map"; socket.target = 'Rmrf'; socket.length = length;
	err = ExgPut(&socket);
	if (!err) {
		if (map.res) {
			err = SendBytes(&socket, MemHandleLock(map.res), MemHandleSize(map.res));
			MemHandleUnlock(map.res);
		} else for (i = 0; i < DmNumRecords(map.db) && !err; i++) {
			MemHandle h = DmQueryRecord(map.db, i);
			if (!h) { err = exgErrBadData; break; }
			err = SendBytes(&socket, MemHandleLock(h), MemHandleSize(h));
			MemHandleUnlock(h);
		}
		ExgDisconnect(&socket, err);
	}
	MapBytesClose(&map); ShowError(err);
}
