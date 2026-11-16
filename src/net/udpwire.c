#include "udpwire.h"
#include "netutil.h"

int udp_port(const char *s, uint16_t *port)
{
	uint32_t n = 0;
	if (!*s) return 0;
	while (*s) {
		if (*s < '0' || *s > '9') return 0;
		n = n * 10 + (*s++ - '0');
		if (n > 65535) return 0;
	}
	if (!n) return 0;
	*port = (uint16_t)n;
	return 1;
}

int udp_ip(const char *s, uint32_t *ip)
{
	uint32_t n = 0;
	int i, v, digits;
	for (i = 0; i < 4; i++) {
		v = digits = 0;
		while (*s >= '0' && *s <= '9') {
			if (++digits > 3) return 0;
			v = v * 10 + *s++ - '0';
		}
		if (!digits || v > 255) return 0;
		n = (n << 8) | v;
		if (i != 3 && *s++ != '.') return 0;
	}
	if (*s || !n || n == 0xffffffffUL || (n >> 24) >= 224) return 0;
	*ip = n;
	return 1;
}

static int SEC_UDP same(UdpAddress a, UdpAddress b)
{
	return a.ip == b.ip && a.port == b.port;
}

void udp_wire_open(UdpWire *w, uint8_t host, uint32_t session, UdpAddress remote)
{
	net_zero(w, sizeof(*w));
	w->host = host;
	w->accepting = host;
	w->session = session ? session : 1;
	if (!host) w->peers[0].address = remote;
}

void udp_wire_sync(UdpWire *w, uint8_t accepting, uint8_t nodes)
{
	uint8_t i;
	w->accepting = accepting;
	// Nodes cannot be reused during a match: late packets must keep their identity.
	if (w->host && accepting)
		for (i = 1; i < LINK_MAX_NODES; i++)
			if (!(nodes & (1 << i))) net_zero(&w->peers[i], sizeof(w->peers[i]));
}

static uint8_t SEC_UDP pack(uint32_t host, uint32_t client, const uint8_t *data,
	uint8_t len, uint8_t *out)
{
	out[0] = 'P'; out[1] = 'B'; out[2] = 'U'; out[3] = 'D';
	out[4] = 1; out[5] = len; out[6] = out[7] = 0;
	net_put32(out + 8, host); net_put32(out + 12, client);
	net_copy(out + UDP_HEADER, data, len);
	return UDP_HEADER + len;
}

uint8_t udp_wire_send(UdpWire *w, uint8_t to, const uint8_t *data, uint8_t len,
	UdpAddress *address, uint8_t *packet)
{
	if (!len || len > LINK_FRAME_MAX || to >= LINK_MAX_NODES) return 0;
	if (w->host) {
		if (!to || !w->peers[to].session) return 0;
		*address = w->peers[to].address;
		return pack(w->session, w->peers[to].session, data, len, packet);
	}
	if (to != 0) return 0;
	*address = w->peers[0].address;
	return pack(w->hostSession, w->session, data, len, packet);
}

uint8_t udp_wire_recv(UdpWire *w, UdpAddress address, const uint8_t *p,
	uint16_t size, uint8_t *from, uint8_t *data, uint8_t *reply, uint8_t *replyLen)
{
	uint8_t len, i, freeNode = 0, reject[15];
	uint32_t host, client;
	*replyLen = 0;
	if (size < UDP_HEADER + 1 || size > UDP_PACKET || p[0] != 'P' || p[1] != 'B' ||
	    p[2] != 'U' || p[3] != 'D' || p[4] != 1 || p[6] || p[7]) return 0;
	len = p[5];
	if (!len || len > LINK_FRAME_MAX || size != UDP_HEADER + len) return 0;
	host = net_get32(p + 8); client = net_get32(p + 12);
	if (!client || !address.ip || !address.port) return 0;
	if (!w->host) {
		if (!same(address, w->peers[0].address) || client != w->session || !host ||
		    (w->hostSession && host != w->hostSession)) return 0;
		w->hostSession = host;
		*from = 0;
	} else {
		if (host && host != w->session) return 0;
		for (i = 1; i < LINK_MAX_NODES; i++) {
			if (!w->peers[i].session) { if (!freeNode) freeNode = i; }
			else if (same(address, w->peers[i].address) && client == w->peers[i].session) break;
		}
		if (i == LINK_MAX_NODES) {
			// Only a complete lockstep HELLO can reserve an endpoint.
			if (p[UDP_HEADER] != 1 || len < 2 || len > 17) return 0;
			if (!w->accepting || !freeNode || p[UDP_HEADER + 1] != 1) {
				net_zero(reject, sizeof(reject)); reject[0] = 9;
				reject[1] = p[UDP_HEADER + 1] != 1 ? 4 : 5;
				*replyLen = pack(w->session, client, reject, sizeof(reject), reply);
				return 0;
			}
			i = freeNode;
			w->peers[i].address = address;
			w->peers[i].session = client;
		}
		// A pre-handshake packet may only repeat HELLO.
		if (!host && p[UDP_HEADER] != 1) return 0;
		*from = i;
	}
	net_copy(data, p + UDP_HEADER, len);
	return len;
}
