#ifndef BLUETOOTH_H
#define BLUETOOTH_H
#include "link.h"
#ifdef __palmos__
#define SEC_BT __attribute__((section("bt")))
#else
#define SEC_BT
#endif
int SEC_BT bt_available(void);
int SEC_BT bt_open(Link *l, uint8_t role);
void SEC_BT bt_close(void);
int SEC_BT bt_send(Link *l, uint8_t to, const uint8_t *buf, uint8_t len);
uint8_t SEC_BT bt_recv(Link *l, uint8_t *from, uint8_t *buf);
void SEC_BT bt_sync(uint8_t accepting, uint8_t nodes);
void SEC_BT bt_service(void);
const char * SEC_BT bt_error(void);
const char * SEC_BT bt_status(void);
#endif
