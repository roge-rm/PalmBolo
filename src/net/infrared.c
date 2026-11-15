// IrLMP carries an ordered byte stream of length-prefixed link frames.
// A 64-byte frame may need two packets on a minimum-size IrLAP link.
#include "../palmbolo.h"
#include <IrLib.h>
#include "infrared.h"

#define IR_FEATURE 16
#define IR_QUEUE 16
#define IR_CREATOR 'Rmrf'

enum { IR_LISTEN, IR_DISCOVER, IR_LAP, IR_QUERY, IR_CONNECT, IR_UP, IR_DEAD };
typedef struct IrFrame {
	UInt8 len, data[LINK_FRAME_MAX];
} IrFrame;

static UInt16 gRef;
static Boolean gOpen, gBound, gBusy, gEver;
static Boolean gServicing;
static UInt8 gRole, gState, gEvent, gQueryDone, gQueryStatus, gPick;
static UInt32 gRetry;
#ifdef DEBUG
static UInt32 gDiagAt;
static UInt16 gSent, gReceived, gFailed;
#endif
static IrConnect gCon;
static IrPacket gPacket;
static UInt8 gWire[LINK_FRAME_MAX], gTxOffset;
static IrFrame gTx[IR_QUEUE], gRx[IR_QUEUE];
static UInt8 gTxHead, gTxCount, gRxHead, gRxCount, gRxOffset;
static IrDeviceAddr gDevice;
static IrIasObject gObject;
static IrIasAttribute gAttr;
static UInt8 gValue[5];
static IrIasQuery gQuery;
static UInt8 gResult[32];
#ifdef IR_TRACE
typedef struct IrTrace {
	UInt32 packet;
	UInt16 len;
	UInt8 event, status, first, second;
} IrTrace;
static IrTrace gTrace[32];
static UInt8 gTraceHead, gTraceCount;

// Callbacks only record memory; polling writes the database log.
static void SEC_NET Trace(UInt8 event, UInt8 status, IrPacket *packet,
	UInt16 len, const UInt8 *data)
{
	IrTrace *t = &gTrace[gTraceHead];
	t->event = event;
	t->status = status;
	t->packet = (UInt32)packet;
	t->len = len;
	t->first = len && data ? data[0] : 0;
	t->second = len > 1 && data ? data[1] : 0;
	gTraceHead = (gTraceHead + 1) % 32;
	if (gTraceCount < 32)
		gTraceCount++;
}
#endif
static const UInt8 kQuery[] = {8, 'P','a','l','m','B','o','l','o', 4,'L','S','A','P'};

static void SEC_NET IrPump(void);

static void SEC_NET IrCallbackBody(IrCallBackParms *p);
static void SEC_NET IrQueryBody(IrStatus status);

// Library callbacks may arrive with the library's A5. Recover ours before
// touching globals or using a jump table entry for another code segment.
static void IrCallback(IrConnect *con, IrCallBackParms *p)
{
	UInt32 app, old;
	(void)con;
	if (FtrGet(IR_CREATOR, IR_FEATURE, &app))
		return;
	old = SysSetA5(app);
	IrCallbackBody(p);
	SysSetA5(old);
}

static void IrQueryCallback(IrStatus status)
{
	UInt32 app, old;
	if (FtrGet(IR_CREATOR, IR_FEATURE, &app))
		return;
	old = SysSetA5(app);
	IrQueryBody(status);
	SysSetA5(old);
}

static void IrQueryBody(IrStatus status)
{
	gQueryStatus = status;
	gQueryDone = 1;
}

static void IrCallbackBody(IrCallBackParms *p)
{
	UInt16 i;
#ifdef IR_TRACE
	Trace(p->event, p->status,
		p->event == LEVENT_PACKET_HANDLED ? p->packet : 0,
		p->event == LEVENT_DATA_IND ? p->rxLen : 0,
		p->event == LEVENT_DATA_IND ? p->rxBuff : 0);
#endif
	if (!gOpen)
		return;
	switch (p->event) {
	case LEVENT_DISCOVERY_CNF:
		if (gState != IR_DISCOVER)
			break;
		if (p->deviceList && p->deviceList->nItems) {
			gDevice = p->deviceList->dev[gPick++ % p->deviceList->nItems].hDevice;
			gEvent = 1;
		} else {
			gState = IR_LISTEN;
		}
		break;
	case LEVENT_LAP_CON_CNF:
		if (gRole == LINK_ROLE_JOIN && gState == IR_LAP)
			gEvent = 2;
		break;
	case LEVENT_LM_CON_IND:
		if (gRole == LINK_ROLE_HOST && !gEver)
			gEvent = 3;
		break;
	case LEVENT_LM_CON_CNF:
		gEvent = 4;
		break;
	case LEVENT_PACKET_HANDLED:
		if (p->packet == &gPacket)
			gBusy = false;
		break;
	case LEVENT_LM_DISCON_IND:
	case LEVENT_LAP_DISCON_IND:
		gBusy = false;
		gTxCount = gRxCount = gTxOffset = gRxOffset = 0;
		gState = gEver ? IR_DEAD : IR_LISTEN;
		gEvent = 0;
		break;
	case LEVENT_DATA_IND:
		if (gState != IR_UP)
			break;
		for (i = 0; i < p->rxLen; i++) {
			IrFrame *f;
			if (gRxCount == IR_QUEUE) {
				// Stop on overflow rather than silently losing reliable data.
				gState = IR_DEAD;
				break;
			}
			f = &gRx[(gRxHead + gRxCount) % IR_QUEUE];
			if (!gRxOffset) {
				f->len = p->rxBuff[i];
				if (!f->len || f->len > LINK_FRAME_MAX) {
					gState = IR_DEAD;
					break;
				}
				gRxOffset = 1;
			} else {
				f->data[gRxOffset++ - 1] = p->rxBuff[i];
				if (gRxOffset == f->len + 1) {
					gRxCount++;
#ifdef DEBUG
					gReceived++;
#endif
					gRxOffset = 0;
				}
			}
		}
		break;
	}
}

int ir_available(void)
{
	UInt16 ref;
	return SysLibFind(irLibName, &ref) == errNone;
}

void ir_close(void)
{
	UInt32 start = TimGetTicks();
	if (!gOpen)
		return;
	// Give queued STOP and LEAVE frames time to reach the other Palm.
	while (gState == IR_UP && (gTxCount || gBusy) &&
	       TimGetTicks() - start < (UInt32)SysTicksPerSecond() * 2) {
		IrPump();
		IrWaitForEvent(gRef, 1);
	}
	gOpen = false;
	IrDisconnectIrLap(gRef);
	if (gBound)
		IrUnbind(gRef, &gCon);
	IrClose(gRef);
	FtrUnregister(IR_CREATOR, IR_FEATURE);
	gBound = false;
}

int ir_open(Link *l, uint8_t role)
{
	UInt32 app;
	ir_close();
	if (SysLibFind(irLibName, &gRef) || IrOpen(gRef, irOpenOptSpeed115200 | irOpenOptDisconnect8))
		return 1;
	asm volatile ("move.l %%a5,%0" : "=r" (app));
	if (FtrSet(IR_CREATOR, IR_FEATURE, app)) {
		IrClose(gRef);
		return 1;
	}
	gOpen = true;
	gEver = gBusy = false;
#ifdef DEBUG
	gDiagAt = TimGetTicks();
	gSent = gReceived = gFailed = 0;
#endif
	gRole = role;
	gState = IR_LISTEN;
	gRetry = TimGetTicks();
	gEvent = gQueryDone = gPick = 0;
	gTxHead = gTxCount = gRxHead = gRxCount = gTxOffset = gRxOffset = 0;
	MemSet(&gCon, sizeof(gCon), 0);
	MemSet(&gPacket, sizeof(gPacket), 0);
	if (IrBind(gRef, &gCon, IrCallback) != IR_STATUS_SUCCESS) {
		ir_close();
		return 1;
	}
	gBound = true;
	IrSetConTypeLMP(&gCon);
	if (role == LINK_ROLE_HOST) {
		gValue[0] = IAS_ATTRIB_INTEGER;
		gValue[1] = gValue[2] = gValue[3] = 0;
		gValue[4] = gCon.lLsap;
		gAttr.name = (UInt8 *)"LSAP";
		gAttr.len = 4;
		gAttr.value = gValue;
		gAttr.valLen = 5;
		gObject.name = (UInt8 *)"PalmBolo";
		gObject.len = 8;
		gObject.nAttribs = 1;
		gObject.attribs = &gAttr;
		if (IrIAS_Add(gRef, &gObject) != IR_STATUS_SUCCESS) {
			ir_close();
			return 1;
		}
	}
	l->node = role == LINK_ROLE_HOST ? 0 : 1;
	l->reliable = 1;
	return 0;
}

void ir_service(void)
{
	UInt8 n;

	if (!gOpen || gServicing)
		return;
	gServicing = true;
	// IrLib normally runs from EvtGetEvent. Keep its receive and TX-done
	// events separate while map loading or drawing keeps the app busy.
	for (n = 0; n < 8 && IrHandleEvent(gRef); n++)
		;
	gServicing = false;
}

static void SEC_NET IrPump(void)
{
	UInt8 ev;
	IrStatus status;
	ir_service();
	ev = gEvent;
	gEvent = 0;
#ifdef DEBUG
	if (TimGetTicks() - gDiagAt >= SysTicksPerSecond()) {
		gDiagAt = TimGetTicks();
#ifdef IR_TRACE
		while (gTraceCount) {
			IrTrace t = gTrace[(gTraceHead + 32 - gTraceCount) % 32];
			gTraceCount--;
			LOG(("irtrace: event %u status %u packet %lx len %u data %x %x",
				t.event, t.status, t.packet, t.len, t.first, t.second));
		}
#endif
		LOG(("ir: state %u busy %u tx %u rx %u sent %u received %u failed %u",
			gState, gBusy, gTxCount, gRxCount, gSent, gReceived, gFailed));
	}
#endif
	if (ev)
		LOG(("ir: action %u state %u", ev, gState));
	if (ev == 1) {
		gState = IR_LAP;
		if (IrConnectIrLap(gRef, gDevice) != IR_STATUS_PENDING)
			gState = IR_LISTEN;
	} else if (ev == 2) {
		MemSet(&gQuery, sizeof(gQuery), 0);
		gQuery.queryBuf = (UInt8 *)kQuery;
		gQuery.queryLen = sizeof(kQuery);
		gQuery.result = gResult;
		gQuery.resultBufSize = sizeof(gResult);
		gQuery.callBack = IrQueryCallback;
		gState = IR_QUERY;
		gQueryDone = 0;
		if (IrIAS_Query(gRef, &gQuery) != IR_STATUS_PENDING)
			IrDisconnectIrLap(gRef);
	} else if (ev == 3 || ev == 4) {
		if (ev == 3) {
			gPacket.buff = gWire;
			gPacket.len = 0;
			gBusy = true;
			status = IrConnectRsp(gRef, &gCon, &gPacket, 0);
			if (status != IR_STATUS_PENDING) {
				gBusy = false;
				IrDisconnectIrLap(gRef);
				return;
			}
		}
		LOG(("ir: connected, tx %u", IrMaxTxSize(gRef, &gCon)));
		gEver = true;
		gState = IR_UP;
	}
	if (gQueryDone && gState == IR_QUERY) {
		gQueryDone = 0;
		if (gQueryStatus == IR_STATUS_SUCCESS && gQuery.retCode == IAS_RET_SUCCESS &&
		    !gQuery.overFlow && gQuery.listLen && gQuery.resultLen >= 7 &&
		    IrIAS_GetType(&gQuery) == IAS_ATTRIB_INTEGER &&
		    IrIAS_GetIntLsap(&gQuery) <= IR_MAX_LSAP) {
			gCon.rLsap = IrIAS_GetIntLsap(&gQuery);
			gPacket.buff = gWire;
			gPacket.len = 0;
			gBusy = true;
			gState = IR_CONNECT;
			if (IrConnectReq(gRef, &gCon, &gPacket, 0) != IR_STATUS_PENDING) {
				gBusy = false;
				IrDisconnectIrLap(gRef);
			}
		} else {
			IrDisconnectIrLap(gRef);
		}
	}
	if (gState == IR_LISTEN && gRole == LINK_ROLE_JOIN &&
	    (Int32)(TimGetTicks() - gRetry) >= 0) {
		gRetry = TimGetTicks() + SysTicksPerSecond() * 2;
		gState = IR_DISCOVER;
		if (IrDiscoverReq(gRef, &gCon) != IR_STATUS_PENDING)
			gState = IR_LISTEN;
	}
	if (gState == IR_UP && !gBusy && gTxCount) {
		IrFrame *f = &gTx[gTxHead];
		UInt16 max = IrMaxTxSize(gRef, &gCon), n = 0;
		UInt8 offset = gTxOffset;
		if (max > sizeof(gWire))
			max = sizeof(gWire);
		if (!max) {
			gState = IR_DEAD;
			return;
		}
		while (n < max && offset <= f->len) {
			gWire[n++] = offset ? f->data[offset - 1] : f->len;
			offset++;
		}
		gPacket.buff = gWire;
		gPacket.len = n;
		gBusy = true;
		status = IrDataReq(gRef, &gCon, &gPacket);
#ifdef IR_TRACE
		Trace(128, status, &gPacket, n, gWire);
#endif
		if (status != IR_STATUS_PENDING) {
#ifdef DEBUG
			if (!gFailed++)
				LOG(("ir: send status %u", status));
#endif
			gBusy = false;
			return;
		}

#ifdef DEBUG
		gSent++;
#endif
		gTxOffset = offset;
		if (offset > f->len) {
			gTxOffset = 0;
			gTxCount--;
			gTxHead = (gTxHead + 1) % IR_QUEUE;
		}
	}
}

int ir_send(Link *l, uint8_t to, const uint8_t *buf, uint8_t len)
{
	IrFrame *f;
	if (!gOpen || to != (l->node ^ 1) || gState != IR_UP || gTxCount == IR_QUEUE)
		return 1;
	f = &gTx[(gTxHead + gTxCount) % IR_QUEUE];
	f->len = len;
	MemMove(f->data, buf, len);
	gTxCount++;
	return 0;
}

uint8_t ir_recv(Link *l, uint8_t *from, uint8_t *buf)
{
	UInt8 len;
	if (!gOpen)
		return 0;
	IrPump();
	if (!gRxCount || gState != IR_UP)
		return 0;
	len = gRx[gRxHead].len;
	MemMove(buf, gRx[gRxHead].data, len);
	gRxCount--;
	gRxHead = (gRxHead + 1) % IR_QUEUE;
	*from = l->node ^ 1;
	return len;
}
