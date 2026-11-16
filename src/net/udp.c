#include <PalmOS.h>
#include <NetMgr.h>
#include "udp.h"

static UInt16 gRef;
static Boolean gOpen;
static NetSocketRef gSocket = -1;
static UdpWire gWire;
static UdpAddress gRemote;
static UInt16 gPort = UDP_PORT;
static Char gError[48];
static UInt32 gSerial;
static UInt8 gBudget;

static void SEC_UDP Error(const Char *s) { StrCopy(gError, s); }
uint8_t udp_available(void) { UInt16 ref; return SysLibFind("Net.lib", &ref) == errNone; }
void udp_config(uint32_t address, uint16_t port) { gRemote.ip = address; gRemote.port = gPort = port; }
const char *udp_error(void) { return gError; }

void udp_close(void)
{
	Err err;
	if (gSocket >= 0) { NetLibSocketClose(gRef, gSocket, 0, &err); gSocket = -1; }
	if (gOpen) { NetLibClose(gRef, false); gOpen = false; }
}

int udp_open(Link *l, uint8_t role)
{
	Err err;
	UInt16 ifErr = 0, before = 0, after = 0;
	Err beforeErr, afterErr;
	Boolean on = true;
	NetSocketAddrINType addr;
	udp_close(); gError[0] = 0;
	if (!gPort || (role == LINK_ROLE_JOIN && !gRemote.ip)) {
		Error("Enter a host address and port."); return 1;
	}
	if (SysLibFind("Net.lib", &gRef) != errNone) { Error("Network library unavailable."); return 1; }
	// NetLib owns the connection dialog and its Cancel button.
	beforeErr = NetLibOpenCount(gRef, &before);
	err = NetLibOpen(gRef, &ifErr);
	afterErr = NetLibOpenCount(gRef, &after);
	gOpen = !beforeErr && !afterErr ? after > before : (err == errNone || err == netErrAlreadyOpen);
	if ((err != errNone && err != netErrAlreadyOpen) || ifErr) { Error("Can't open the network connection."); udp_close(); return 1; }
	gSocket = NetLibSocketOpen(gRef, netSocketAddrINET, netSocketTypeDatagram, 0, 0, &err);
	if (gSocket < 0) { Error("Can't open a UDP socket."); goto fail; }
	if (NetLibSocketOptionSet(gRef, gSocket, netSocketOptLevelSocket,
	    netSocketOptSockNonBlocking, &on, sizeof(on), 0, &err) < 0) {
		Error("Can't set non-blocking UDP."); goto fail;
	}
	MemSet(&addr, sizeof(addr), 0);
	addr.family = netSocketAddrINET;
	addr.port = NetHToNS(role == LINK_ROLE_HOST ? gPort : 0);
	if (NetLibSocketBind(gRef, gSocket, (NetSocketAddrType *)&addr, sizeof(addr), 0, &err) < 0) {
		Error("Can't bind UDP port. Try another."); goto fail;
	}
	udp_wire_open(&gWire, role == LINK_ROLE_HOST,
		TimGetSeconds() ^ TimGetTicks() ^ (++gSerial * 0x9e3779b9UL), gRemote);
	l->node = role == LINK_ROLE_HOST ? 0 : 1;
	l->reliable = 0;
	return 0;
fail:
	udp_close(); return 1;
}

static int SEC_UDP Send(UdpAddress to, UInt8 *data, UInt16 len)
{
	NetSocketAddrINType addr;
	Err err;
	Int16 sent;
	MemSet(&addr, sizeof(addr), 0);
	addr.family = netSocketAddrINET;
	addr.addr = NetHToNL(to.ip); addr.port = NetHToNS(to.port);
	sent = NetLibSend(gRef, gSocket, data, len, 0, &addr, sizeof(addr), 0, &err);
	// An unreachable peer expires through lockstep; the shared socket stays open.
	if (sent < 0 && err != netErrWouldBlock && err != netErrTimeout && err != netErrUnreachableDest)
		Error("UDP send failed. Connection closed.");
	return sent == len ? 0 : 1;
}

int udp_send(Link *l, uint8_t to, const uint8_t *buf, uint8_t len)
{
	UInt8 packet[UDP_PACKET], n;
	UdpAddress address;
	(void)l;
	if (gSocket < 0) return 1;
	n = udp_wire_send(&gWire, to, buf, len, &address, packet);
	return n ? Send(address, packet, n) : 1;
}

uint8_t udp_recv(Link *l, uint8_t *from, uint8_t *buf)
{
	UInt8 packet[UDP_PACKET + 1], reply[UDP_PACKET], replyLen, n, tries;
	NetSocketAddrINType addr;
	UdpAddress address;
	UInt16 addrLen;
	Err err;
	Int16 size;
	(void)l;
	if (gSocket < 0) return 0;
	// A bounded batch lets the UI run even when unrelated datagrams arrive.
	for (tries = 0; tries < 16 && gBudget; tries++) {
		gBudget--;
		addrLen = sizeof(addr);
		size = NetLibReceive(gRef, gSocket, packet, sizeof(packet), 0, &addr, &addrLen, 0, &err);
		if (size < 0) {
			if (err == netErrMessageTooBig || err == netErrBufTooSmall || err == netErrUnreachableDest) continue;
			if (err != netErrWouldBlock && err != netErrTimeout)
				Error("UDP receive failed. Connection closed.");
			return 0;
		}
		if (addrLen < sizeof(addr) || addr.family != netSocketAddrINET) continue;
		address.ip = NetNToHL(addr.addr); address.port = NetNToHS(addr.port);
		n = udp_wire_recv(&gWire, address, packet, (UInt16)size, from, buf, reply, &replyLen);
		if (replyLen) Send(address, reply, replyLen);
		if (n) return n;
	}
	return 0;
}

void udp_sync(uint8_t accepting, uint8_t nodes)
{
	gBudget = 32;
	udp_wire_sync(&gWire, accepting, nodes);
}

void udp_local(char *text)
{
	UInt16 i, instance, len;
	UInt32 creator, ip = 0;
	Char address[16];
	if (gSocket < 0) { StrCopy(text, "Network is closed."); return; }
	for (i = 0; i < 16; i++) {
		if (NetLibIFGet(gRef, i, &creator, &instance) != errNone) break;
		len = sizeof(ip);
		if (NetLibIFSettingGet(gRef, creator, instance, netIFSettingActualIPAddr, &ip, &len) == errNone &&
		    len == sizeof(ip) && ip && NetNToHL(ip) != 0x7f000001UL) break;
		ip = 0;
	}
	if (ip) {
		NetLibAddrINToA(gRef, ip, address);
		StrPrintF(text, "%s:%u", address, gPort);
	} else StrPrintF(text, "Address unavailable. Port %u", gPort);
}
