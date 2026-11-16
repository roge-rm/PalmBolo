#ifndef MAP_TEST_PALM
#define MAP_TEST_PALM
#include "../netstub/PalmOS.h"
#include <stdlib.h>
#include <strings.h>
typedef UInt32 LocalID;
typedef struct Handle *MemHandle;
typedef struct Database *DmOpenRef;
typedef struct Form FormType;
typedef struct List ListType;
typedef struct { UInt32 length, target; Char *name,*description,*type; } ExgSocketType;
typedef struct { MemHandle res; DmOpenRef db; } MapBytes;
#define MemPtrNew malloc
#define MemPtrFree free
#define MemMove memmove
static inline char *StrNCopy(char *d,const char *s,size_t n) { size_t i; for(i=0;i<n&&s[i];i++)d[i]=s[i]; if(i<n)d[i]=0; return d; }
#define StrLen strlen
#define StrCompare strcmp
#define StrCaselessCompare strcasecmp
static inline char *StrCat(char *d,const char *s) { size_t n=strlen(d),i=0; do { d[n+i]=s[i]; } while(s[i++]); return d; }
#define dmMaxRecordIndex 65535
#define dmModeReadWrite 3
#define dmHdrAttrBackup 8
#define noListSelection -1
#define exgErrBadData 1
#define exgErrDeviceFull 2
#define exgMemError 3
#define exgErrUserCancel 4
#define exgErrDisconnected 5
#define sysFtrCreator 1
#define sysFtrNumROMVersion 2
#define exgRegExtensionID 1
#define exgRegTypeID 2
UInt16 DmNumRecords(DmOpenRef);
MemHandle DmQueryRecord(DmOpenRef, UInt16);
UInt32 MemHandleSize(MemHandle);
void *MemHandleLock(MemHandle);
Err MemHandleUnlock(MemHandle);
LocalID DmFindDatabase(UInt16,const Char *);
Err DmCreateDatabase(UInt16,const Char *,UInt32,UInt32,Boolean);
DmOpenRef DmOpenDatabase(UInt16,LocalID,UInt16);
MemHandle DmNewRecord(DmOpenRef,UInt16 *,UInt32);
Err DmWrite(void *,UInt32,const void *,UInt32);
Err DmReleaseRecord(DmOpenRef,UInt16,Boolean);
Err DmCloseDatabase(DmOpenRef);
Err DmDeleteDatabase(UInt16,LocalID);
Err DmSetDatabaseInfo(UInt16,LocalID,const Char *,UInt16 *,void *,void *,void *,void *,void *,void *,void *,UInt32 *,void *);
Err ExgAccept(ExgSocketType *);
Err ExgDisconnect(ExgSocketType *,Err);
UInt32 ExgReceive(ExgSocketType *,void *,UInt32,Err *);
UInt32 ExgSend(ExgSocketType *,const void *,UInt32,Err *);
Err ExgPut(ExgSocketType *);
Err ExgRegisterData(UInt32,UInt16,const Char *);
Err FtrGet(UInt32,UInt16,UInt32 *);
UInt16 FrmCustomAlert(UInt16,const Char *,const Char *,const Char *);
FormType *FrmGetActiveForm(void);
FormType *FrmInitForm(UInt16);
void FrmSetActiveForm(FormType *);
void *FrmGetObjectPtr(FormType *,UInt16);
UInt16 FrmGetObjectIndex(FormType *,UInt16);
void LstSetListChoices(ListType *,Char **,UInt16);
void LstSetSelection(ListType *,Int16);
void FrmCopyLabel(FormType *,UInt16,const Char *);
UInt16 FrmDoDialog(FormType *);
Int16 LstGetSelection(ListType *);
void FrmDeleteForm(FormType *);
Boolean MapFileSum(UInt16,const Char *,UInt32 *,UInt32 *);
Boolean MapBytesOpen(MapBytes *,UInt16,const Char *);
void MapBytesClose(MapBytes *);
typedef struct { UInt16 eType; union { struct { UInt16 controlID; } ctlSelect; } data; } EventType;
enum { nilEvent, appStopEvent, ctlSelectEvent };
void EvtGetEvent(EventType *,Int32);
void EvtAddEventToQueue(EventType *);
Boolean SysHandleEvent(EventType *);
Boolean FrmHandleEvent(FormType *,EventType *);
void FrmDrawForm(FormType *);
#endif
