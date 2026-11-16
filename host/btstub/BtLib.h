// SDK declarations used by the adapter; the Palm build uses the real SDK headers.
#ifndef TEST_BT_LIB_H
#define TEST_BT_LIB_H
#include <PalmOS.h>
#define btLibName "Bluetooth Library"
#define btLibFeatureCreator 0x626c7468UL
#define btLibFeatureVersion 0
#define BT_L2CAP_RANDOM_PSM 0xffff
#define btLibErrAlreadyOpen 0x3104
#define btLibErrFailed 0x3106
#define btLibErrPending 0x310a
#define btLibErrRadioFatal 0x310d
#define btLibErrRadioInitialized 0x310e
#define btLibErrAlreadyConnected 0x3162
#define btLibErrCanceled 0x3127
#define btLibErrBusy 0x3128
#define btLibErrNoAclLink 0x3112
#define btLibErrSocket 0x3121
#define btLibErrNoPiconet 0x315f
#define btLibErrOutOfMemory 0x3105

typedef Int16 BtLibSocketRef;
typedef UInt16 BtLibL2CapPsmType;
typedef MemHandle BtLibSdpRecordHandle;
typedef UInt32 BtLibClassOfDeviceType;
typedef struct { UInt8 address[6]; } BtLibDeviceAddressType;
typedef enum { btLibUuidSize128=16 } BtLibSdpUuidSizeEnum;
typedef struct { BtLibSdpUuidSizeEnum size; UInt8 UUID[16]; } BtLibSdpUuidType;
typedef enum { btLibL2CapProtocol, btLibRfCommProtocol, btLibSdpProtocol } BtLibProtocolEnum;
typedef enum { btLibManagementEventRadioState=1, btLibManagementEventACLDisconnect=5,
 btLibManagementEventACLConnectOutbound=7, btLibManagementEventPiconetCreated=8 } BtLibManagementEventEnum;
typedef struct { BtLibManagementEventEnum event; Err status; union { BtLibDeviceAddressType bdAddr; } eventData; } BtLibManagementEventType;
typedef void (*BtLibManagementProcPtr)(BtLibManagementEventType *, UInt32);
typedef enum { btLibSocketEventConnectRequest=1, btLibSocketEventConnectedOutbound,
 btLibSocketEventConnectedInbound, btLibSocketEventDisconnected, btLibSocketEventData,
 btLibSocketEventSendComplete, btLibSocketEventSdpGetPsmByUuid=15 } BtLibSocketEventEnum;
typedef struct { BtLibSocketEventEnum event; BtLibSocketRef socket; Err status;
 union { struct { UInt16 dataLen; UInt8 *data; } data; BtLibSocketRef newSocket;
 BtLibDeviceAddressType requestingDevice;
 struct { UInt32 remoteHandle; union { BtLibL2CapPsmType psm; UInt8 channel; } param; } sdpByUuid;
 } eventData;
} BtLibSocketEventType;
typedef void (*BtLibSocketProcPtr)(BtLibSocketEventType *, UInt32);
typedef struct { union { struct { BtLibL2CapPsmType localPsm; UInt16 localMtu,minRemoteMtu; } L2Cap; } data; } BtLibSocketListenInfoType;
typedef struct { BtLibDeviceAddressType *remoteDeviceP; union { struct { BtLibL2CapPsmType remotePsm; UInt16 minRemoteMtu,localMtu; } L2Cap; } data; } BtLibSocketConnectInfoType;
Err BtLibOpen(UInt16, Boolean);
Err BtLibClose(UInt16);
Err BtLibRegisterManagementNotification(UInt16, BtLibManagementProcPtr, UInt32);
Err BtLibUnregisterManagementNotification(UInt16, BtLibManagementProcPtr);
Err BtLibPiconetCreate(UInt16, Boolean, Boolean);
Err BtLibPiconetDestroy(UInt16);
Err BtLibDiscoverSingleDevice(UInt16, Char *, BtLibClassOfDeviceType *, UInt8, BtLibDeviceAddressType *, Boolean, Boolean);
Err BtLibLinkConnect(UInt16, BtLibDeviceAddressType *);
Err BtLibLinkDisconnect(UInt16, BtLibDeviceAddressType *);
Err BtLibSocketCreate(UInt16, BtLibSocketRef *, BtLibSocketProcPtr, UInt32, BtLibProtocolEnum);
Err BtLibSocketClose(UInt16, BtLibSocketRef);
Err BtLibSocketListen(UInt16, BtLibSocketRef, BtLibSocketListenInfoType *);
Err BtLibSocketConnect(UInt16, BtLibSocketRef, BtLibSocketConnectInfoType *);
Err BtLibSocketRespondToConnection(UInt16, BtLibSocketRef, Boolean);
Err BtLibSocketSend(UInt16, BtLibSocketRef, UInt8 *, UInt32);
Err BtLibSdpGetPsmByUuid(UInt16, BtLibSocketRef, BtLibDeviceAddressType *, BtLibSdpUuidType *, UInt8);
Err BtLibSdpServiceRecordCreate(UInt16, BtLibSdpRecordHandle *);
Err BtLibSdpServiceRecordDestroy(UInt16, BtLibSdpRecordHandle);
Err BtLibSdpServiceRecordStartAdvertising(UInt16, BtLibSdpRecordHandle);
Err BtLibSdpServiceRecordStopAdvertising(UInt16, BtLibSdpRecordHandle);
Err BtLibSdpServiceRecordSetAttributesForSocket(UInt16, BtLibSocketRef, BtLibSdpUuidType *, UInt8, const Char *, UInt16, BtLibSdpRecordHandle);
#endif
