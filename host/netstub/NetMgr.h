#ifndef TEST_NETMGR_H
#define TEST_NETMGR_H
#include "PalmOS.h"
#include <arpa/inet.h>
typedef Int16 NetSocketRef;
typedef struct { UInt16 family, port; UInt32 addr; UInt8 zero[8]; } NetSocketAddrINType;
typedef NetSocketAddrINType NetSocketAddrType;
enum { netErrAlreadyOpen=1, netErrWouldBlock, netErrTimeout, netErrMessageTooBig,
 netErrBufTooSmall, netErrUnreachableDest, netErrNotOpen, netErrSocketNotOpen,
 netErrNoInterfaces, netErrInterfaceDown, netErrInterfaceNotFound, netErrPPPBroughtDown,
 netErrQuitOnTxFail, netErrOutOfPackets, netErrOutOfMemory, netErrOutOfResources,
 netErrSocketBusy, netSocketAddrINET=2, netSocketTypeDatagram=2,
 netSocketOptLevelSocket=0xffff, netSocketOptSockNonBlocking=0x2000,
 netIFSettingActualIPAddr=0x1002 };
#define NetHToNS htons
#define NetHToNL htonl
#define NetNToHS ntohs
#define NetNToHL ntohl
Err NetLibOpen(UInt16, UInt16 *);
Err NetLibOpenCount(UInt16, UInt16 *);
Err NetLibClose(UInt16, UInt16);
NetSocketRef NetLibSocketOpen(UInt16, int, int, Int16, Int32, Err *);
Int16 NetLibSocketClose(UInt16, NetSocketRef, Int32, Err *);
Int16 NetLibSocketOptionSet(UInt16, NetSocketRef, UInt16, UInt16, void *, UInt16, Int32, Err *);
Int16 NetLibSocketBind(UInt16, NetSocketRef, NetSocketAddrType *, Int16, Int32, Err *);
Int16 NetLibSend(UInt16, NetSocketRef, void *, UInt16, UInt16, void *, UInt16, Int32, Err *);
Int16 NetLibReceive(UInt16, NetSocketRef, void *, UInt16, UInt16, void *, UInt16 *, Int32, Err *);
Err NetLibIFGet(UInt16, UInt16, UInt32 *, UInt16 *);
Err NetLibIFSettingGet(UInt16, UInt32, UInt16, UInt16, void *, UInt16 *);
Char *NetLibAddrINToA(UInt16, UInt32, Char *);
#endif
