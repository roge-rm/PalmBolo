#ifndef UDPWIRE_H
#define UDPWIRE_H
#include "link.h"
#ifdef __palmos__
#define SEC_UDP __attribute__((section("udp")))
#else
#define SEC_UDP
#endif
#define UDP_PORT 5046
#define UDP_HEADER 16
#define UDP_PACKET (UDP_HEADER + LINK_FRAME_MAX)

typedef struct UdpAddress { uint32_t ip; uint16_t port; } UdpAddress;
typedef struct UdpPeer { UdpAddress address; uint32_t session; } UdpPeer;
typedef struct UdpWire {
	UdpPeer peers[LINK_MAX_NODES];
	uint32_t session, hostSession;
	uint8_t host, accepting;
} UdpWire;

int SEC_UDP udp_ip(const char *text, uint32_t *ip);
int SEC_UDP udp_port(const char *text, uint16_t *port);
void SEC_UDP udp_wire_open(UdpWire *w, uint8_t host, uint32_t session, UdpAddress remote);
void SEC_UDP udp_wire_sync(UdpWire *w, uint8_t accepting, uint8_t nodes);
uint8_t SEC_UDP udp_wire_send(UdpWire *w, uint8_t to, const uint8_t *data, uint8_t len,
	UdpAddress *address, uint8_t *packet);
// Returns payload size, or zero. A rejected join has replyLen bytes to send back.
uint8_t SEC_UDP udp_wire_recv(UdpWire *w, UdpAddress address, const uint8_t *packet,
	uint16_t size, uint8_t *from, uint8_t *data, uint8_t *reply, uint8_t *replyLen);
#endif
