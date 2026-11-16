#ifndef UDP_H
#define UDP_H
#include "udpwire.h"
uint8_t SEC_UDP udp_available(void);
void SEC_UDP udp_config(uint32_t address, uint16_t port);
int SEC_UDP udp_open(Link *l, uint8_t role);
void SEC_UDP udp_close(void);
int SEC_UDP udp_send(Link *l, uint8_t to, const uint8_t *buf, uint8_t len);
uint8_t SEC_UDP udp_recv(Link *l, uint8_t *from, uint8_t *buf);
void SEC_UDP udp_sync(uint8_t accepting, uint8_t nodes);
const char * SEC_UDP udp_error(void);
void SEC_UDP udp_local(char *text);
#endif
