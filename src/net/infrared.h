#ifndef INFRARED_H
#define INFRARED_H

#include "link.h"

int SEC_NET ir_available(void);
int SEC_NET ir_open(Link *l, uint8_t role);
int SEC_NET ir_send(Link *l, uint8_t to, const uint8_t *buf, uint8_t len);
uint8_t SEC_NET ir_recv(Link *l, uint8_t *from, uint8_t *buf);
void SEC_NET ir_close(void);
void SEC_NET ir_service(void);
#if defined(DEBUG) && defined(IR_TEST_CLOSE_QUEUE)
void SEC_NET ir_test_close_queue(void);
#endif

#endif
