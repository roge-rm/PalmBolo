// One L2CAP packet is one link frame. Buffers belong to BtLib until send completion.
#include <PalmOS.h>
#include <BtLib.h>
#include "bluetooth.h"
#include "netutil.h"

#define BT_FEATURE 17
#define BT_CREATOR 'Rmrf'
#define BT_PEERS 4
#define BT_RX 8
#define BT_WAIT 30

enum { BT_OFF, BT_PICONET, BT_LISTEN, BT_ACL, BT_QUERY, BT_CONNECT, BT_UP, BT_DEAD };
typedef struct BtFrame { UInt8 len, data[LINK_FRAME_MAX]; } BtFrame;
typedef struct BtPeer {
	BtLibSocketRef socket;
	BtLibDeviceAddressType address;
	UInt32 since;
	UInt8 used, up, dead, busy, seated, acl, head, count, txLen;
	UInt8 tx[LINK_FRAME_MAX];
	BtFrame rx[BT_RX];
} BtPeer;
static UInt16 gRef;
static Boolean gOpen, gRegistered, gPiconet, gAdvertised, gAclOwned, gClosing, gServicing;
static UInt8 gHost, gState, gAccepting, gPending, gNext;
static UInt32 gEpoch, gSince;
static BtLibSocketRef gListener = -1, gSdp = -1;
static BtLibSdpRecordHandle gRecord;
static BtLibDeviceAddressType gRemote;
static BtLibL2CapPsmType gPsm;
static BtPeer gPeers[BT_PEERS];
static Char gError[48];
static const BtLibSdpUuidType gUuid = { btLibUuidSize128,
	{0xd7,0x1c,0x89,0x23,0xa6,0x4b,0x4e,0x80,0xb0,0x10,0x52,0x6d,0x72,0x66,0x00,0x01} };

static void SEC_BT SocketBody(BtLibSocketEventType *e, UInt32 epoch);
static void SEC_BT ManagementBody(BtLibManagementEventType *e, UInt32 epoch);

// The library can call with its own A5; the first-segment entry restores ours.
static void SocketCallback(BtLibSocketEventType *e, UInt32 epoch)
{
#ifdef __palmos__
	UInt32 app, old;
	if (FtrGet(BT_CREATOR, BT_FEATURE, &app)) return;
	old = SysSetA5(app);
#endif
	SocketBody(e, epoch);
#ifdef __palmos__
	SysSetA5(old);
#endif
}
static void ManagementCallback(BtLibManagementEventType *e, UInt32 epoch)
{
#ifdef __palmos__
	UInt32 app, old;
	if (FtrGet(BT_CREATOR, BT_FEATURE, &app)) return;
	old = SysSetA5(app);
#endif
	ManagementBody(e, epoch);
#ifdef __palmos__
	SysSetA5(old);
#endif
}

static void SEC_BT Error(const Char *s) { StrCopy(gError, s); gState = BT_DEAD; }
static UInt32 SEC_BT Now(void) { return TimGetTicks(); }
static Boolean SEC_BT Expired(UInt32 since) { return Now() - since >= (UInt32)SysTicksPerSecond() * BT_WAIT; }
static Boolean SEC_BT Same(const BtLibDeviceAddressType *a, const BtLibDeviceAddressType *b)
{
	UInt16 i;
	for (i = 0; i < sizeof(a->address); i++) if (a->address[i] != b->address[i]) return false;
	return true;
}
static void SEC_BT CloseSocket(BtLibSocketRef *s)
{
	BtLibSocketRef old = *s;
	*s = -1;
	if (old >= 0) BtLibSocketClose(gRef, old);
}
static void SEC_BT Drop(UInt8 i)
{
	BtPeer *p = &gPeers[i];
	CloseSocket(&p->socket);
	if (gHost && p->acl) {
		p->acl = false;
		BtLibLinkDisconnect(gRef, &p->address);
	}
	p->up = p->busy = p->count = 0;
	p->dead = true;
}
static void SEC_BT Connected(UInt8 i, BtLibSocketRef socket)
{
	BtPeer *p = &gPeers[i];
	p->socket = socket;
	p->up = true;
	p->since = Now();
	if (!gHost) gState = BT_UP;
}
static void SocketBody(BtLibSocketEventType *e, UInt32 epoch)
{
	UInt8 i;
	BtPeer *p;
	if (!gOpen || gClosing || epoch != gEpoch) return;
	if (e->socket == gListener && gListener >= 0) {
		if (e->event == btLibSocketEventConnectRequest) {
			Err err;
			UInt8 pick = 0;
			if (gAccepting && !gPending && gState == BT_LISTEN) {
				for (i = 1; i < BT_PEERS; i++) {
					if (gPeers[i].used && Same(&gPeers[i].address, &e->eventData.requestingDevice)) break;
					if (!gPeers[i].used && !pick) pick = i;
				}
				if (i != BT_PEERS) pick = 0;
			}
			if (pick) {
				p = &gPeers[pick]; net_zero(p, sizeof(*p)); p->socket = -1;
				p->used = p->acl = true; p->address = e->eventData.requestingDevice; p->since = Now();
				gPending = pick;
			}
			err = BtLibSocketRespondToConnection(gRef, gListener, pick != 0);
			if (pick && err != btLibErrPending && err != errNone) {
				gPeers[pick].dead = true; gPending = 0;
			}
		} else if (e->event == btLibSocketEventConnectedInbound) {
			if (!e->status && gPending && gAccepting) Connected(gPending, e->eventData.newSocket);
			else {
				if (!e->status) BtLibSocketClose(gRef, e->eventData.newSocket);
				if (gPending) gPeers[gPending].dead = true;
			}
			gPending = 0;
		}
		// Disconnected may finish another request's rejection. An unsuccessful
		// acceptance is cleaned up by its peer deadline or ACL disconnect.
		return;
	}
	if (e->socket == gSdp && gSdp >= 0) {
		if (e->event == btLibSocketEventSdpGetPsmByUuid && gState == BT_QUERY) {
			if (e->status || !e->eventData.sdpByUuid.param.psm) Error("No PalmBolo game on that device.");
			else { gPsm = e->eventData.sdpByUuid.param.psm; gState = BT_CONNECT; }
		} else if (e->event == btLibSocketEventDisconnected) {
			gSdp = -1;
			if (gState == BT_QUERY) Error("Bluetooth service search failed.");
		}
		return;
	}
	for (i = 0; i < BT_PEERS; i++) if (gPeers[i].used && gPeers[i].socket == e->socket) break;
	if (i == BT_PEERS) return;
	p = &gPeers[i];
	switch (e->event) {
	case btLibSocketEventConnectedOutbound:
		if (e->status) p->dead = true;
		else Connected(i, e->socket);
		break;
	case btLibSocketEventDisconnected:
		p->socket = -1; p->dead = true; p->up = p->busy = p->count = 0;
		break;
	case btLibSocketEventData:
		if (!p->up || p->dead) break;
		if (e->status || !e->eventData.data.data || !e->eventData.data.dataLen ||
		    e->eventData.data.dataLen > LINK_FRAME_MAX || p->count == BT_RX) {
			// Never silently lose a frame on a reliable link.
			p->dead = true;
		} else {
			BtFrame *f = &p->rx[(p->head + p->count) % BT_RX];
			f->len = (UInt8)e->eventData.data.dataLen;
			net_copy(f->data, e->eventData.data.data, f->len); p->count++;
		}
		break;
	case btLibSocketEventSendComplete:
		if (!p->busy || e->eventData.data.data != p->tx || e->eventData.data.dataLen != p->txLen) break;
		p->busy = false;
		if (e->status) p->dead = true;
		break;
	default: break;
	}
}
static void ManagementBody(BtLibManagementEventType *e, UInt32 epoch)
{
	UInt8 i;
	if (!gOpen || gClosing || epoch != gEpoch) return;
	if (e->event == btLibManagementEventPiconetCreated && gState == BT_PICONET) {
		if (e->status) { gPiconet = false; Error("Can't host a Bluetooth game."); }
		else gState = BT_LISTEN;
	} else if (e->event == btLibManagementEventACLConnectOutbound && !gHost &&
	           gState == BT_ACL && Same(&gRemote, &e->eventData.bdAddr)) {
		if (e->status) Error("Can't connect to the Bluetooth host.");
		else gState = BT_QUERY;
	} else if (e->event == btLibManagementEventACLDisconnect) {
		for (i = 0; i < BT_PEERS; i++) if (gPeers[i].used && Same(&gPeers[i].address, &e->eventData.bdAddr)) {
			gPeers[i].acl = false; gPeers[i].dead = true;
		}
		if (!gHost && Same(&gRemote, &e->eventData.bdAddr)) Error("Bluetooth host disconnected.");
	} else if (e->event == btLibManagementEventRadioState && e->status != btLibErrRadioInitialized && e->status != errNone)
		Error("Bluetooth radio is unavailable.");
}

int bt_available(void)
{
	UInt32 version; UInt16 ref;
	return FtrGet(btLibFeatureCreator, btLibFeatureVersion, &version) == errNone &&
		SysLibFind(btLibName, &ref) == errNone;
}
const char *bt_error(void) { return gError; }
const char *bt_status(void)
{
	switch (gState) {
	case BT_PICONET: return "Preparing Bluetooth";
	case BT_LISTEN: return "Waiting for players";
	case BT_ACL: return "Connecting to device";
	case BT_QUERY: return "Finding PalmBolo game";
	case BT_CONNECT: return "Connecting to game";
	default: return "Looking for a game";
	}
}

// Peek at library keys without removing or reordering application input.
void bt_service(void)
{
	EventType e;
	UInt8 i;
	if (!gOpen || gServicing) return;
	gServicing = true;
	for (i = 0; i < 8; i++) {
		net_zero(&e, sizeof(e));
		if (EvtDequeueKeyEvent((SysEventType *)&e, true) ||
		    e.eType != keyDownEvent || !(e.data.keyDown.modifiers & libEvtHookKeyMask)) break;
		if (EvtDequeueKeyEvent((SysEventType *)&e, false)) break;
		SysHandleEvent(&e);
	}
	gServicing = false;
}

void bt_close(void)
{
	UInt8 i, busy;
	UInt32 start;
	if (!gOpen) return;
	gAccepting = false;
	start = Now();
	do {
		bt_service(); busy = false;
		for (i = 0; i < BT_PEERS; i++) if (gPeers[i].busy && !gPeers[i].dead) busy = true;
		if (busy) SysTaskDelay(1);
	} while (busy && Now() - start < (UInt32)SysTicksPerSecond() * 2);
	gClosing = true;
	if (gAdvertised) BtLibSdpServiceRecordStopAdvertising(gRef, gRecord);
	if (gRecord) BtLibSdpServiceRecordDestroy(gRef, gRecord);
	gAdvertised = false; gRecord = NULL;
	CloseSocket(&gSdp); CloseSocket(&gListener);
	for (i = 0; i < BT_PEERS; i++) CloseSocket(&gPeers[i].socket);
	if (gRegistered) BtLibUnregisterManagementNotification(gRef, ManagementCallback);
	gRegistered = false;
	if (gPiconet) BtLibPiconetDestroy(gRef);
	else if (gAclOwned) BtLibLinkDisconnect(gRef, &gRemote);
	gPiconet = gAclOwned = false;
	BtLibClose(gRef);
	gOpen = false; gState = BT_OFF;
#ifdef __palmos__
	FtrUnregister(BT_CREATOR, BT_FEATURE);
#endif
}

int bt_open(Link *l, uint8_t role)
{
	Err err; UInt8 i;
#ifdef __palmos__
	UInt32 app;
#endif
	bt_close();
	gError[0] = 0; gClosing = false; gPending = gNext = 0;
	gHost = role == LINK_ROLE_HOST; gAccepting = gHost;
	gListener = gSdp = -1; gRecord = NULL;
	net_zero(gPeers, sizeof(gPeers));
	for (i = 0; i < BT_PEERS; i++) gPeers[i].socket = -1;
	if (!bt_available() || SysLibFind(btLibName, &gRef)) { Error("Bluetooth is unavailable."); return 1; }
	err = BtLibOpen(gRef, false);
	if (err && err != btLibErrAlreadyOpen) { Error("Can't open Bluetooth."); return 1; }
	gOpen = true;
	// Hosting owns a piconet; do not take over another application's open library.
	if (gHost && err == btLibErrAlreadyOpen) { Error("Bluetooth is in use. Try again later."); goto fail; }
#ifdef __palmos__
	asm volatile ("move.l %%a5,%0" : "=r" (app));
	if (FtrSet(BT_CREATOR, BT_FEATURE, app)) { Error("Can't register Bluetooth callbacks."); goto fail; }
#endif
	if (!++gEpoch) ++gEpoch;
	if (BtLibRegisterManagementNotification(gRef, ManagementCallback, gEpoch)) { Error("Can't register Bluetooth callbacks."); goto fail; }
	gRegistered = true; gSince = Now();
	if (gHost) {
		gState = BT_PICONET;
		err = BtLibPiconetCreate(gRef, true, true);
		if (err != errNone && err != btLibErrPending) { Error("Can't create Bluetooth piconet."); goto fail; }
		gPiconet = true;
		if (!err) gState = BT_LISTEN;
	} else {
		err = BtLibDiscoverSingleDevice(gRef, "Choose the PalmBolo host", NULL, 0, &gRemote, false, false);
		if (err) { Error(err == btLibErrCanceled ? "Bluetooth selection cancelled." : "Bluetooth discovery failed."); goto fail; }
		gState = BT_ACL; gSince = Now();
		err = BtLibLinkConnect(gRef, &gRemote);
		gAclOwned = err == btLibErrPending || err == errNone;
		if (err == btLibErrAlreadyConnected || err == errNone) gState = BT_QUERY;
		else if (err != btLibErrPending) { Error("Can't connect to the Bluetooth host."); goto fail; }
	}
	l->node = gHost ? 0 : 1; l->reliable = 1;
	return 0;
fail:
	bt_close(); return 1;
}

void bt_sync(uint8_t accepting, uint8_t nodes)
{
	UInt8 i;
	Err err;
	if (!gOpen || gClosing) return;
	gAccepting = gHost && accepting;
	if (gState != BT_LISTEN && gState != BT_UP && gState != BT_DEAD && Expired(gSince))
		Error("Bluetooth connection timed out.");
	if (gState == BT_LISTEN && gListener < 0) {
		BtLibSocketListenInfoType info;
		net_zero(&info, sizeof(info));
		info.data.L2Cap.localPsm = BT_L2CAP_RANDOM_PSM;
		info.data.L2Cap.localMtu = info.data.L2Cap.minRemoteMtu = LINK_FRAME_MAX;
		if (BtLibSocketCreate(gRef, &gListener, SocketCallback, gEpoch, btLibL2CapProtocol) ||
		    BtLibSocketListen(gRef, gListener, &info) ||
		    BtLibSdpServiceRecordCreate(gRef, &gRecord) ||
		    BtLibSdpServiceRecordSetAttributesForSocket(gRef, gListener, (BtLibSdpUuidType *)&gUuid, 1, "PalmBolo", 8, gRecord) ||
		    BtLibSdpServiceRecordStartAdvertising(gRef, gRecord)) Error("Can't advertise the Bluetooth game.");
		else gAdvertised = true;
	}
	if (gState == BT_QUERY && gSdp < 0) {
		if (BtLibSocketCreate(gRef, &gSdp, SocketCallback, gEpoch, btLibSdpProtocol)) Error("Can't search for the Bluetooth game.");
		else {
			err = BtLibSdpGetPsmByUuid(gRef, gSdp, &gRemote, (BtLibSdpUuidType *)&gUuid, 1);
			if (err != btLibErrPending) Error("Can't find the Bluetooth game.");
		}
	}
	if (gState == BT_CONNECT && !gPeers[0].used) {
		BtLibSocketConnectInfoType info;
		BtPeer *p = &gPeers[0];
		CloseSocket(&gSdp);
		p->used = true; p->since = Now(); p->address = gRemote;
		net_zero(&info, sizeof(info)); info.remoteDeviceP = &gRemote;
		info.data.L2Cap.remotePsm = gPsm;
		info.data.L2Cap.localMtu = info.data.L2Cap.minRemoteMtu = LINK_FRAME_MAX;
		if (BtLibSocketCreate(gRef, &p->socket, SocketCallback, gEpoch, btLibL2CapProtocol)) Error("Can't open Bluetooth game socket.");
		else {
			err = BtLibSocketConnect(gRef, p->socket, &info);
			if (err != btLibErrPending) Error("Host unavailable or game full.");
		}
	}
	for (i = 0; i < BT_PEERS; i++) {
		BtPeer *p = &gPeers[i];
		if (!p->used) continue;
		if (gHost && (nodes & (1 << i))) p->seated = true;
		if ((gHost && !p->seated && Expired(p->since)) || p->dead ||
		    (gHost && accepting && p->seated && !(nodes & (1 << i)))) {
			Drop(i);
			if (!gHost) Error("Bluetooth host disconnected or full.");
			else if (accepting && !(nodes & (1 << i))) {
				if (gPending == i) gPending = 0;
				p->used = false;
			}
		}
	}
}

int bt_send(Link *l, uint8_t to, const uint8_t *buf, uint8_t len)
{
	BtPeer *p; Err err;
	(void)l;
	if (!gOpen || gState == BT_DEAD || !len || len > LINK_FRAME_MAX || to >= BT_PEERS ||
	    (gHost ? !to : to != 0)) return 1;
	p = &gPeers[to];
	if (!p->up || p->dead || p->busy) return 1;
	net_copy(p->tx, buf, len); p->txLen = len; p->busy = true;
	err = BtLibSocketSend(gRef, p->socket, p->tx, len);
	if (err == btLibErrPending) return 0;
	p->busy = false;
	if (err == errNone) return 0;
	if (err != btLibErrBusy) p->dead = true;
	return 1;
}
uint8_t bt_recv(Link *l, uint8_t *from, uint8_t *buf)
{
	UInt8 j, i, len;
	(void)l;
	if (!gOpen || gState == BT_DEAD) return 0;
	for (j = 0; j < BT_PEERS; j++) {
		i = (gNext + j) % BT_PEERS;
		if (gPeers[i].up && !gPeers[i].dead && gPeers[i].count) {
			BtPeer *p = &gPeers[i]; BtFrame *f = &p->rx[p->head];
			len = f->len; net_copy(buf, f->data, len); *from = i;
			p->head = (p->head + 1) % BT_RX; p->count--; gNext = (i + 1) % BT_PEERS;
			return len;
		}
	}
	return 0;
}
