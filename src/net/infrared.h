#ifndef INFRARED_H
#define INFRARED_H

#include "link.h"

int SEC_NET ir_available(void);
int SEC_NET ir_open(Link *l, uint8_t role);
int SEC_NET ir_send(Link *l, uint8_t to, const uint8_t *buf, uint8_t len);
uint8_t SEC_NET ir_recv(Link *l, uint8_t *from, uint8_t *buf);
void SEC_NET ir_close(void);
void SEC_NET ir_service(void);
// True once the session has ended and every received frame has been read.
int SEC_NET ir_dead(void);
// True when the session has ended and the link won't take another.
int SEC_NET ir_lost(void);
// Host: accepting is set in the lobby, where a joiner that went away can be
// replaced once its seat is free; nodes has a bit for each seated node.
void SEC_NET ir_sync(uint8_t accepting, uint8_t nodes);
#if defined(DEBUG) && defined(IR_TEST_CLOSE_QUEUE)
void SEC_NET ir_test_close_queue(void);
#endif

#endif
