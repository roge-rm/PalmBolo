// Links between Palms: one small interface for every way of connecting.
// Plain C, no Palm headers, so the host test uses it too.
//
// A link carries frames of at most LINK_FRAME_MAX bytes between nodes.
// The host is node 0 and each player who joins gets a node of their own.
// A joiner only talks to the host; the host talks to every node.

#ifndef LINK_H
#define LINK_H

#include <stdint.h>

#ifdef __palmos__
#define SEC_NET     __attribute__((section("net")))
#else
#define SEC_NET

#endif

#define LINK_FRAME_MAX  64      // IrLib's frame size
#define LINK_HOST       0       // the host's node
#define LINK_MAX_NODES  8

// Kinds of link.
enum { LINK_LOOP = 0, LINK_IR = 1, LINK_UDP = 2, LINK_BT = 3 };
enum { LINK_ROLE_HOST = 0, LINK_ROLE_JOIN = 1 };

// An open link end. reliable is 1 when frames arrive in order and none are
// lost (IrLAP, L2CAP), 0 when they can be lost, repeated or reordered (UDP).
typedef struct Link {
	uint8_t kind, end, node, reliable;
} Link;

// Opens an end as host or joiner. Returns 0, or non-zero if it can't.
int SEC_NET link_open(Link *l, uint8_t kind, uint8_t role);

// Sends a frame of len bytes (1 to LINK_FRAME_MAX) to node to. Returns 0,
// or non-zero if the frame couldn't be queued.
int SEC_NET link_send(Link *l, uint8_t to, const uint8_t *buf, uint8_t len);

// Takes the next frame that has arrived into buf (LINK_FRAME_MAX bytes)
// and returns its length, with the sender's node in *from; 0 when none.
uint8_t SEC_NET link_recv(Link *l, uint8_t *from, uint8_t *buf);

void SEC_NET link_close(Link *l);

// The loopback: up to LINK_MAX_NODES ends in one process. Frames wait in a
// queue for a delay picked between minMs and maxMs. lossPm in 1000 frames
// are lost; with reorder set, frames can overtake each other and some
// arrive twice, as over UDP. Without it they keep their order, as over
// IrLAP. Time is whatever the caller last gave loop_time, in ms.
void SEC_NET loop_reset(uint32_t seed, uint16_t minMs, uint16_t maxMs, uint16_t lossPm,
	uint8_t reorder);
void SEC_NET loop_time(uint32_t ms);
// Stops delivering to and from node, as if it were switched off (1) or
// back (0).
void SEC_NET loop_cut(uint8_t node, uint8_t cut);
// Frames sent and lost so far.
uint32_t SEC_NET loop_sent(void);
uint32_t SEC_NET loop_lost(void);

#ifndef __palmos__
// Host tests: limit queued frames from each node, or reject its next sends.
void loop_limit(uint16_t frames);
void loop_reject(uint8_t node, uint16_t sends);
uint32_t loop_rejected(void);
#endif

#endif
