// The link interface and the loopback link. The loopback is built on the
// host, and on the Palm only for LOOPBACK_TEST.
#include "link.h"
#include "netutil.h"
#ifdef __palmos__
#include "infrared.h"
#include "bluetooth.h"
#include "udp.h"
#endif

#if !defined(__palmos__) || defined(LOOPBACK_TEST)
#define HAVE_LOOP 1
#endif

#ifdef HAVE_LOOP

#if defined(__palmos__) && defined(DEBUG)
#define LOOP_QUEUE  32
#else
#define LOOP_QUEUE  512
#endif

typedef struct LoopFrame {
	uint32_t at;            // ms it can be taken
	uint32_t seq;           // order sent, for ties
	uint8_t used, from, to, len;
	uint8_t data[LINK_FRAME_MAX];
} LoopFrame;

static LoopFrame gQ[LOOP_QUEUE];
static uint32_t gNow, gSeq, gRng = 1, gSent, gLost;
static uint32_t gLast[LINK_MAX_NODES][LINK_MAX_NODES];  // latest at sent from, to
static uint16_t gMin, gMax, gLoss;
static uint8_t gReorder, gOpen, gCut;
#ifndef __palmos__
static uint16_t gLimit, gReject[LINK_MAX_NODES];
static uint32_t gRejected;

void loop_limit(uint16_t frames) { gLimit = frames; }
void loop_reject(uint8_t node, uint16_t sends) { gReject[node] = sends; }
uint32_t loop_rejected(void) { return gRejected; }
#endif
static uint16_t gWaiting[LINK_MAX_NODES];  // frames queued to each node

static uint32_t SEC_NET loop_rand(void)
{
	gRng ^= gRng << 13;
	gRng ^= gRng >> 17;
	gRng ^= gRng << 5;
	return gRng;
}

void loop_reset(uint32_t seed, uint16_t minMs, uint16_t maxMs, uint16_t lossPm, uint8_t reorder)
{
	net_zero(gQ, sizeof(gQ));
	net_zero(gLast, sizeof(gLast));
	net_zero(gWaiting, sizeof(gWaiting));
	gRng = seed ? seed : 1;
	gMin = minMs;
	gMax = maxMs < minMs ? minMs : maxMs;
	gLoss = lossPm;
	gReorder = reorder;
	gOpen = gCut = 0;
	gSeq = gSent = gLost = 0;
#ifndef __palmos__
	gLimit = 0;
	net_zero(gReject, sizeof(gReject));
	gRejected = 0;
#endif
}

void loop_time(uint32_t ms)
{
	gNow = ms;
}

void loop_cut(uint8_t node, uint8_t cut)
{
	if (cut)
		gCut = (uint8_t)(gCut | (1 << node));
	else
		gCut = (uint8_t)(gCut & ~(1 << node));
}

uint32_t loop_sent(void)
{
	return gSent;
}

uint32_t loop_lost(void)
{
	return gLost;
}

static int SEC_NET loop_put(uint8_t from, uint8_t to, const uint8_t *buf, uint8_t len)
{
	uint16_t i;
	uint32_t at = gNow + gMin + (gMax > gMin ? loop_rand() % (uint32_t)(gMax - gMin + 1) : 0);

	for (i = 0; i < LOOP_QUEUE && gQ[i].used; i++)
		;
	if (i == LOOP_QUEUE)
		return 1;
	// Kept in order: nothing overtakes a frame sent before it.
	if (!gReorder && at < gLast[from][to])
		at = gLast[from][to];
	gLast[from][to] = at;
	gQ[i].used = 1;
	gWaiting[to]++;
	gQ[i].at = at;
	gQ[i].seq = gSeq++;
	gQ[i].from = from;
	gQ[i].to = to;
	gQ[i].len = len;
	net_copy(gQ[i].data, buf, len);
	return 0;
}

static int SEC_NET loop_send(Link *l, uint8_t to, const uint8_t *buf, uint8_t len)
{
#ifndef __palmos__
	if (gReject[l->node]) {
		gReject[l->node]--;
		gRejected++;
		return 1;
	}
	if (gLimit) {
		uint16_t i, queued = 0;

		for (i = 0; i < LOOP_QUEUE; i++)
			if (gQ[i].used && gQ[i].from == l->node)
				queued++;
		if (queued >= gLimit) {
			gRejected++;
			return 1;
		}
	}
#endif
	gSent++;
	if (((gCut >> l->node) | (gCut >> to)) & 1)
		return 0;
	if (gLoss && loop_rand() % 1000 < gLoss) {
		gLost++;
		return 0;
	}
	if (loop_put(l->node, to, buf, len) != 0) {
		// A full queue on a lossy link is one more loss.
		if (gReorder)
			return 0;
		return 1;
	}
	// Over UDP the odd frame arrives twice.
	if (gReorder && gLoss && loop_rand() % 1000 < gLoss / 2)
		loop_put(l->node, to, buf, len);
	return 0;
}

static uint8_t SEC_NET loop_recv(Link *l, uint8_t *from, uint8_t *buf)
{
	uint16_t i, best = LOOP_QUEUE;
	uint8_t len;

	if (!gWaiting[l->node])
		return 0;
	for (i = 0; i < LOOP_QUEUE; i++) {
		LoopFrame *f = &gQ[i];

		if (!f->used || f->to != l->node || f->at > gNow)
			continue;
		if (best == LOOP_QUEUE || f->at < gQ[best].at ||
		    (f->at == gQ[best].at && f->seq < gQ[best].seq))
			best = i;
	}
	if (best == LOOP_QUEUE)
		return 0;
	gQ[best].used = 0;
	gWaiting[l->node]--;
	if ((gCut >> l->node) & 1)
		return 0;
	*from = gQ[best].from;
	len = gQ[best].len;
	net_copy(buf, gQ[best].data, len);
	return len;
}

#endif

int link_open(Link *l, uint8_t kind, uint8_t role)
{
	net_zero(l, sizeof(*l));
	l->kind = kind;
#ifdef __palmos__
	if (kind == LINK_BT)
		return bt_open(l, role);
	if (kind == LINK_IR)
		return ir_open(l, role);
	if (kind == LINK_UDP)
		return udp_open(l, role);
#endif
#ifdef HAVE_LOOP
	if (kind == LINK_LOOP) {
		if (role == LINK_ROLE_HOST) {
			l->node = LINK_HOST;
		} else {
			uint8_t n;

			for (n = 1; n < LINK_MAX_NODES && ((gOpen >> n) & 1); n++)
				;
			if (n == LINK_MAX_NODES)
				return 1;
			l->node = n;
		}
		gOpen = (uint8_t)(gOpen | (1 << l->node));
		l->reliable = !gReorder && !gLoss;
		return 0;
	}
#else
	(void)role;
#endif
	return 1;
}

int link_send(Link *l, uint8_t to, const uint8_t *buf, uint8_t len)
{
#ifdef __palmos__
	if (l->kind == LINK_UDP)
		return udp_send(l, to, buf, len);
	if (l->kind == LINK_BT)
		return bt_send(l, to, buf, len);
	if (l->kind == LINK_IR && len && len <= LINK_FRAME_MAX)
		return ir_send(l, to, buf, len);
#endif
	if (len == 0 || len > LINK_FRAME_MAX || to >= LINK_MAX_NODES)
		return 1;
#ifdef HAVE_LOOP
	if (l->kind == LINK_LOOP)
		return loop_send(l, to, buf, len);
#else
	(void)l;
	(void)buf;
#endif
	return 1;
}

uint8_t link_recv(Link *l, uint8_t *from, uint8_t *buf)
{
#ifdef __palmos__
	if (l->kind == LINK_BT)
		return bt_recv(l, from, buf);
	if (l->kind == LINK_IR)
		return ir_recv(l, from, buf);
	if (l->kind == LINK_UDP)
		return udp_recv(l, from, buf);
#endif
#ifdef HAVE_LOOP
	if (l->kind == LINK_LOOP)
		return loop_recv(l, from, buf);
#else
	(void)l;
	(void)from;
	(void)buf;
#endif
	return 0;
}

void link_close(Link *l)
{
#ifdef __palmos__
	if (l->kind == LINK_BT)
		bt_close();
	if (l->kind == LINK_IR)
		ir_close();
	if (l->kind == LINK_UDP)
		udp_close();
#endif
#ifdef HAVE_LOOP
	if (l->kind == LINK_LOOP)
		gOpen = (uint8_t)(gOpen & ~(1 << l->node));
#endif
	l->kind = 0xFF;
}
