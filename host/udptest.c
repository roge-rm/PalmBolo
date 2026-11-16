// UDP framing and eight-peer lockstep over real loopback datagram sockets.
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include "../src/net/udpwire.h"
#include "../src/net/lockstep.h"
#include "../src/game/mapstore.h"
#include "../src/game/util.h"

void mapstore_host_bank(int n);
typedef struct Dev {
	int socket, ready, gone;
	UdpAddress address;
	UdpWire wire;
	Link link;
	Lockstep ls;
	GameState game;
	uint8_t map[65535];
	uint16_t have;
} Dev;
static Dev dev[9];
static uint8_t source[65535];
static uint16_t sourceLen;
static unsigned sent, lost, refused;

static struct sockaddr_in sockaddr(UdpAddress a)
{
	struct sockaddr_in s;
	memset(&s, 0, sizeof(s)); s.sin_family = AF_INET;
	s.sin_addr.s_addr = htonl(a.ip); s.sin_port = htons(a.port);
	return s;
}
static UdpAddress endpoint(struct sockaddr_in a)
{
	UdpAddress r = {ntohl(a.sin_addr.s_addr), ntohs(a.sin_port)}; return r;
}
int link_send(Link *l, uint8_t to, const uint8_t *b, uint8_t len)
{
	Dev *d = &dev[l->end];
	uint8_t packet[UDP_PACKET], n;
	UdpAddress dest;
	struct sockaddr_in address;
	if (++sent % 29 == 0) { refused++; return 1; }
	n = udp_wire_send(&d->wire, to, b, len, &dest, packet);
	assert(n);
	if (sent % 17 == 0) { lost++; return 0; }
	address = sockaddr(dest);
	assert(sendto(d->socket, packet, n, 0, (struct sockaddr *)&address, sizeof(address)) == n);
	return 0;
}
uint8_t link_recv(Link *l, uint8_t *from, uint8_t *b)
{
	Dev *d = &dev[l->end];
	uint8_t packet[UDP_PACKET + 1], reply[UDP_PACKET], replyLen, n;
	struct sockaddr_in a;
	for (int tries = 0; tries < 32; tries++) {
		socklen_t alen = sizeof(a);
		ssize_t count = recvfrom(d->socket, packet, sizeof(packet), MSG_DONTWAIT, (struct sockaddr *)&a, &alen);
		if (count < 0) { assert(errno == EAGAIN || errno == EWOULDBLOCK); return 0; }
		n = udp_wire_recv(&d->wire, endpoint(a), packet, count, from, b, reply, &replyLen);
		if (replyLen) assert(sendto(d->socket, reply, replyLen, 0, (struct sockaddr *)&a, alen) == replyLen);
		if (n) return n;
	}
	return 0;
}
static void open_dev(int i)
{
	Dev *d = &dev[i];
	struct sockaddr_in a = sockaddr((UdpAddress){0x7f000001, 0});
	socklen_t len = sizeof(a);
	memset(d, 0, sizeof(*d));
	d->socket = socket(AF_INET, SOCK_DGRAM, 0); assert(d->socket >= 0);
	assert(bind(d->socket, (struct sockaddr *)&a, sizeof(a)) == 0);
	assert(getsockname(d->socket, (struct sockaddr *)&a, &len) == 0);
	d->address = endpoint(a);
	udp_wire_open(&d->wire, i == 0, 1000 + i, dev[0].address);
	d->link.kind = LINK_UDP; d->link.end = i; d->link.node = i ? 1 : 0;
	if (!i) ls_host_open(&d->ls, &d->link, "Host");
	else ls_join_open(&d->ls, &d->link, "Guest");
}
static void sync_host(void)
{
	uint8_t mask = 0;
	for (int i = 1; i < 8; i++) if (dev[0].ls.seated & (1 << i)) mask |= 1 << dev[0].ls.node[i];
	udp_wire_sync(&dev[0].wire, dev[0].ls.state == LS_LOBBY, mask);
}
static void poll_all(uint32_t now)
{
	sync_host();
	for (int i = 0; i < 9; i++) if (dev[i].socket >= 0 && !dev[i].gone) ls_poll(&dev[i].ls, now);
	sync_host();
}
static void get_map(void *ctx, uint16_t off, uint8_t *dst, uint8_t n)
{
	(void)ctx; memcpy(dst, source + off, n);
}
static uint8_t put_map(void *ctx, uint16_t off, const uint8_t *src, uint8_t n)
{
	Dev *d = ctx;
	assert(off == d->have && (unsigned)off + n <= sourceLen);
	memcpy(d->map + off, src, n); d->have += n; return 0;
}
static void setup(int i)
{
	MapSource input; MapMemory memory; GameSetup gs;
	mapstore_host_bank(i); mapstore_open(TERRAIN_DEEP_SEA);
	map_source_memory(&input, &memory, source, sourceLen);
	assert(map_parse(&dev[i].game.map, &input) == MAP_OK);
	ls_setup(&dev[i].ls.start, &gs);
	game_init(&dev[i].game, dev[i].ls.start.seed, &gs);
	dev[i].ready = 1;
}
static void integration(void)
{
	FILE *f = fopen("res/everard.map", "rb"); assert(f);
	sourceLen = fread(source, 1, sizeof(source), f); fclose(f); assert(sourceLen);
	for (int i = 0; i < 9; i++) dev[i].socket = -1;
	for (int i = 0; i < 8; i++) open_dev(i);
	ls_host_lobby(&dev[0].ls, "Everard Island", 0, 0, get_map, NULL);
	uint32_t now;
	for (now = 0; now < 4000; now += 10) poll_all(now);
	assert(dev[0].ls.seated == 255);
	for (int i = 1; i < 8; i++) assert(dev[i].ls.inLobby);
	open_dev(8);
	for (; now < 5000; now += 10) poll_all(now);
	assert(dev[8].ls.stop == LS_STOP_FULL);
	close(dev[8].socket); dev[8].socket = -1;
	// Leave the lobby, then use a new endpoint and nonce to take the free seat.
	ls_leave(&dev[7].ls);
	for (; now < 6000; now += 10) poll_all(now);
	assert(dev[0].ls.seated == 127);
	close(dev[7].socket); open_dev(7);
	dev[7].wire.session += 100;
	for (; now < 7500; now += 10) poll_all(now);
	assert(dev[0].ls.seated == 255);
	LsStart start; memset(&start, 0, sizeof(start));
	start.seed = 71; start.mapLen = sourceLen;
	start.mapSum = util_crc(0xffffffffUL, source, sourceLen) ^ 0xffffffffUL;
	start.delay = 3; start.dropSecs = 3; start.sumTurns = 16;
	ls_host_start(&dev[0].ls, &start); setup(0);
	open_dev(8);
	int left = 0;
	for (; now < 90000; now += 10) {
		poll_all(now);
		for (int i = 1; i < 8; i++) if (!dev[i].ready && dev[i].ls.state == LS_STARTING) {
			if (dev[i].ls.fetch == LS_MAP_NONE) ls_join_fetch(&dev[i].ls, put_map, &dev[i]);
			if (dev[i].ls.fetch == LS_MAP_GOT) {
				assert(dev[i].have == sourceLen && !memcmp(dev[i].map, source, sourceLen));
				setup(i); ls_join_ready(&dev[i].ls);
			}
		}
		if (!left && dev[0].game.tick >= 512) { ls_leave(&dev[7].ls); dev[7].gone = 1; left = 1; }
		for (int i = 0; i < 8; i++) if (dev[i].ready && !dev[i].gone && dev[i].game.tick < 1024 && ls_ready(&dev[i].ls)) {
			GameInput input; memset(&input, 0, sizeof(input));
			input.keys = dev[i].game.tick % 40 < 20 ? IN_FASTER | IN_LEFT : IN_FIRE;
			mapstore_host_bank(i); ls_tick(&dev[i].ls, &dev[i].game, &input);
		}
		int done = 1;
		for (int i = 0; i < 7; i++) if (dev[i].game.tick != 1024) done = 0;
		if (done) break;
	}
	assert(now < 90000 && left && dev[8].ls.stop == LS_STOP_FULL);
	uint32_t sum = game_checksum(&dev[0].game), terrain;
	mapstore_host_bank(0); terrain = game_terrain_checksum();
	for (int i = 0; i < 7; i++) {
		assert(dev[i].ls.state == LS_PLAYING && dev[i].ls.sumsCompared > 0);
		assert(game_checksum(&dev[i].game) == sum);
		mapstore_host_bank(i); assert(game_terrain_checksum() == terrain);
	}
	ls_leave(&dev[0].ls);
	for (uint32_t end = now + 1000; now < end; now += 10) poll_all(now);
	for (int i = 1; i < 7; i++) assert(dev[i].ls.stop == LS_STOP_HOST_LEFT);
	for (int i = 0; i < 9; i++) if (dev[i].socket >= 0) close(dev[i].socket);
	printf("UDP eight peers: map bytes, 1024 ticks/checksums, full/late rejection, lobby reuse, player/host leave; %u lost, %u backpressure sends\n", lost, refused);
}
static void framing(void)
{
	UdpWire host, client;
	UdpAddress a = {0x7f000001,5046}, b = {0x7f000001,5047}, dest;
	uint8_t packet[UDP_PACKET + 1], reply[UDP_PACKET], data[64], from, rn, n;
	uint8_t hello[] = {1,1,'A'};
	uint32_t ip; uint16_t port;
	assert(udp_ip("192.168.1.2", &ip) && ip == 0xc0a80102);
	assert(!udp_ip("1.2.3", &ip) && !udp_ip("1.2.3.256", &ip) && !udp_ip("224.1.1.1", &ip));
	assert(!udp_ip("1.2.3.4x", &ip) && !udp_ip("0.0.0.0", &ip));
	assert(udp_port("65535", &port) && !udp_port("65536", &port) && !udp_port("0", &port));
	udp_wire_open(&host, 1, 44, a); udp_wire_open(&client, 0, 55, a);
	n = udp_wire_send(&client, 0, hello, sizeof(hello), &dest, packet);
	for (int i = 0; i < n; i++) assert(!udp_wire_recv(&host,b,packet,i,&from,data,reply,&rn));
	assert(!host.peers[1].session);
	packet[4] = 2; assert(!udp_wire_recv(&host,b,packet,n,&from,data,reply,&rn)); packet[4] = 1;
	assert(udp_wire_recv(&host,b,packet,n,&from,data,reply,&rn) == sizeof(hello) && from == 1);
	n = udp_wire_send(&host, 1, hello, sizeof(hello), &dest, packet);
	assert(!udp_wire_recv(&client,b,packet,n,&from,data,reply,&rn));
	assert(udp_wire_recv(&client,a,packet,n,&from,data,reply,&rn) == sizeof(hello));
	assert(client.hostSession == 44);
	packet[15] ^= 1; assert(!udp_wire_recv(&client,a,packet,n,&from,data,reply,&rn));
	memset(data, 3, 64); n = udp_wire_send(&client,0,data,64,&dest,packet);
	assert(n == UDP_PACKET && udp_wire_recv(&host,b,packet,n,&from,data,reply,&rn) == 64);
	assert(!udp_wire_recv(&host,b,packet,n+1,&from,data,reply,&rn));
	udp_wire_sync(&host,1,0); assert(!host.peers[1].session);
	assert(!udp_wire_recv(&host,b,packet,n,&from,data,reply,&rn));
	udp_wire_sync(&host,0,0);
	n = udp_wire_send(&client,0,hello,sizeof(hello),&dest,packet);
	assert(!udp_wire_recv(&host,b,packet,n,&from,data,reply,&rn) && rn);
	assert(udp_wire_recv(&client,a,reply,rn,&from,data,packet,&n) == 15 && data[1] == LS_STOP_FULL);
	puts("UDP framing: addresses, malformed/truncated/oversized packets, sender/session checks and admission passed");
}
int main(void) { framing(); integration(); return 0; }
