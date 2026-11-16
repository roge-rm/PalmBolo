#include <PalmOS.h>
typedef UInt32 FileRef;
typedef struct { UInt32 attributes; Char *nameP; UInt16 nameBufLen; } FileInfoType;
#define sysFileCVFSMgr 3
#define vfsFtrIDVersion 1
#define vfsModeRead 2
#define vfsFileAttrDirectory 16
#define vfsIteratorStart 0
#define vfsIteratorStop 0xffffffffUL
#define vfsErrFileEOF 6
#define expErrEnumerationEmpty 7
Err VFSFileRead(FileRef,UInt32,void *,UInt32 *);
Err VFSFileOpen(UInt16,const Char *,UInt16,FileRef *);
Err VFSFileClose(FileRef);
Err VFSFileSize(FileRef,UInt32 *);
Err VFSVolumeEnumerate(UInt16 *,UInt32 *);
Err VFSDirEntryEnumerate(FileRef,UInt32 *,FileInfoType *);
