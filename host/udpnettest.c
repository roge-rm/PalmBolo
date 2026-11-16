// Exercise the production Palm adapter with controlled NetLib results.
#include <assert.h>
#include "netstub/NetMgr.h"
#include "../src/net/udp.h"
#include "../src/net/lockstep.h"
static struct {
 int missing, count, increment, openError, interfaceError, socketError, optionError, bindError;
 int closes, sockets, sockCloses, receiveError, sendError, receiveCount, oversized;
 UInt16 boundPort;
 UInt8 packet[UDP_PACKET]; UInt16 packetLen; int sent;
 // Scripted results for NetLibReceive, taken in order before the default.
 struct { Err err; UInt16 len, alen, family, port; UInt32 ip; UInt8 data[UDP_PACKET+1]; } rx[8];
 int rxCount, rxNext;
} f;
static void script(Err err, const UInt8 *data, UInt16 len, UInt32 ip, UInt16 port)
{
 assert(f.rxCount<8 && len<=sizeof(f.rx[0].data));
 f.rx[f.rxCount].err=err; f.rx[f.rxCount].len=len; f.rx[f.rxCount].ip=ip; f.rx[f.rxCount].port=port;
 f.rx[f.rxCount].alen=sizeof(NetSocketAddrINType); f.rx[f.rxCount].family=netSocketAddrINET;
 if (len) memcpy(f.rx[f.rxCount].data,data,len);
 f.rxCount++;
}
// A wire packet carrying a payload between the two sessions.
static UInt16 wire(UInt8 *out, UInt32 host, UInt32 client, const UInt8 *payload, UInt8 len)
{
 UInt32 h=htonl(host), c=htonl(client);
 memcpy(out,"PBUD",4); out[4]=1; out[5]=len; out[6]=out[7]=0;
 memcpy(out+8,&h,4); memcpy(out+12,&c,4); memcpy(out+UDP_HEADER,payload,len);
 return UDP_HEADER+len;
}
Err SysLibFind(const Char *name, UInt16 *ref) { assert(!strcmp(name,"Net.lib")); *ref=7; return f.missing; }
UInt32 TimGetSeconds(void) { return 1700000000; }
UInt32 TimGetTicks(void) { return 5000; }
Err NetLibOpenCount(UInt16 ref, UInt16 *count) { assert(ref==7); *count=f.count; return 0; }
Err NetLibOpen(UInt16 ref, UInt16 *errors) { assert(ref==7); f.count+=f.increment; *errors=f.interfaceError; return f.openError; }
Err NetLibClose(UInt16 ref, UInt16 immediate) { assert(ref==7 && !immediate && f.count); f.count--; f.closes++; return 0; }
NetSocketRef NetLibSocketOpen(UInt16 ref, int family, int type, Int16 protocol, Int32 timeout, Err *err)
{ assert(ref==7 && family==netSocketAddrINET && type==netSocketTypeDatagram && !protocol && !timeout); *err=f.socketError; if (*err) return -1; f.sockets++; return 3; }
Int16 NetLibSocketClose(UInt16 ref, NetSocketRef socket, Int32 timeout, Err *err)
{ assert(ref==7 && socket==3 && !timeout); f.sockCloses++; *err=0; return 0; }
Int16 NetLibSocketOptionSet(UInt16 ref, NetSocketRef socket, UInt16 level, UInt16 option, void *value, UInt16 length, Int32 timeout, Err *err)
{ assert(ref==7 && socket==3 && level==netSocketOptLevelSocket && option==netSocketOptSockNonBlocking && length==sizeof(Boolean) && *(Boolean *)value && !timeout); *err=f.optionError; return *err?-1:0; }
Int16 NetLibSocketBind(UInt16 ref, NetSocketRef socket, NetSocketAddrType *a, Int16 len, Int32 timeout, Err *err)
{ assert(ref==7 && socket==3 && len==sizeof(*a) && a->family==netSocketAddrINET && !a->addr && !timeout); f.boundPort=ntohs(a->port); *err=f.bindError; return *err?-1:0; }
Int16 NetLibSend(UInt16 ref, NetSocketRef socket, void *data, UInt16 len, UInt16 flags, void *address, UInt16 alen, Int32 timeout, Err *err)
{ NetSocketAddrINType *a=address; assert(ref==7 && socket==3 && !flags && !timeout && alen==sizeof(*a)); assert(ntohl(a->addr)==0xc0a80102 && ntohs(a->port)==5050); *err=f.sendError; if (*err) return -1; assert(len<=UDP_PACKET); memcpy(f.packet,data,len); f.packetLen=len; f.sent++; return len; }
Int16 NetLibReceive(UInt16 ref, NetSocketRef socket, void *data, UInt16 len, UInt16 flags, void *address, UInt16 *alen, Int32 timeout, Err *err)
{ assert(ref==7 && socket==3 && len==UDP_PACKET+1 && !flags && !timeout && *alen==sizeof(NetSocketAddrINType)); f.receiveCount++;
 if (f.rxNext<f.rxCount) {
  NetSocketAddrINType *a=address; int i=f.rxNext++;
  if (f.rx[i].err) { *err=f.rx[i].err; return -1; }
  memcpy(data,f.rx[i].data,f.rx[i].len); memset(a,0,sizeof(*a));
  a->family=f.rx[i].family; a->addr=htonl(f.rx[i].ip); a->port=htons(f.rx[i].port); *alen=f.rx[i].alen; *err=0;
  return f.rx[i].len;
 }
 if (f.oversized) {f.oversized--; *err=netErrMessageTooBig;} else *err=f.receiveError?f.receiveError:netErrWouldBlock; return -1; }
Err NetLibIFGet(UInt16 ref, UInt16 index, UInt32 *creator, UInt16 *instance)
{ assert(ref==7); *creator=1; *instance=0; return index?99:0; }
Err NetLibIFSettingGet(UInt16 ref, UInt32 creator, UInt16 instance, UInt16 setting, void *value, UInt16 *len)
{ assert(ref==7 && creator==1 && instance==0 && setting==netIFSettingActualIPAddr && *len==4); *(UInt32 *)value=htonl(0xc0a80103); return 0; }
Char *NetLibAddrINToA(UInt16 ref, UInt32 ip, Char *out)
{ assert(ref==7 && ntohl(ip)==0xc0a80103); strcpy(out,"192.168.1.3"); return out; }
static void reset(void) { udp_close(); memset(&f,0,sizeof(f)); f.increment=1; udp_config(0xc0a80102,5050); }
int main(void)
{
 Link link; char text[48]; uint8_t from, data[64], hello[]={1,LS_VERSION,'A'}, seats[]={3,1,0,0,0}, big[UDP_PACKET+1];
 UInt8 packet[UDP_PACKET+1]; UInt16 n; UInt32 session; int sent;
 reset(); assert(udp_available()); f.missing=1; assert(!udp_available() && udp_open(&link,0)); assert(!f.closes);
 reset(); f.openError=99; f.increment=0; f.count=3; assert(udp_open(&link,0)); assert(f.count==3 && !f.closes);
 reset(); f.openError=99; f.count=3; assert(udp_open(&link,0)); assert(f.count==3 && f.closes==1);
 reset(); f.interfaceError=99; assert(udp_open(&link,0)); assert(!f.count && f.closes==1);
 reset(); f.socketError=99; assert(udp_open(&link,0)); assert(f.closes==1 && !f.sockCloses);
 reset(); f.optionError=99; assert(udp_open(&link,0)); assert(f.closes==1 && f.sockCloses==1);
 reset(); f.bindError=99; assert(udp_open(&link,0)); assert(f.closes==1 && f.sockCloses==1);
 reset(); f.count=3; f.openError=netErrAlreadyOpen; assert(!udp_open(&link,0) && f.boundPort==5050 && !link.reliable);
 udp_local(text); assert(!strcmp(text,"192.168.1.3:5050")); udp_close(); udp_close(); assert(f.count==3 && f.closes==1 && f.sockCloses==1);
 reset(); assert(!udp_open(&link,1) && !f.boundPort); f.sendError=netErrWouldBlock;
 assert(udp_send(&link,0,hello,sizeof(hello)) && !udp_error()[0]); f.sendError=0;
 f.sendError=netErrUnreachableDest; assert(udp_send(&link,0,hello,sizeof(hello)) && !udp_error()[0]); f.sendError=0;
 assert(!udp_send(&link,0,hello,sizeof(hello)) && f.packetLen==UDP_HEADER+sizeof(hello));
 udp_sync(0,0); f.oversized=40; assert(!udp_recv(&link,&from,data) && !udp_error()[0] && f.receiveCount==16);
 assert(!udp_recv(&link,&from,data) && f.receiveCount==32); assert(!udp_recv(&link,&from,data) && f.receiveCount==32);
 f.oversized=0; f.receiveError=netErrUnreachableDest; udp_sync(0,0);
 assert(!udp_recv(&link,&from,data) && !udp_error()[0] && f.receiveCount==48);
 f.receiveError=0; assert(!udp_recv(&link,&from,data) && !udp_error()[0]);
 assert(!udp_send(&link,0,hello,sizeof(hello)) && !f.sockCloses);
 // Shortages and unknown errors are lost packets. A closed socket or interface ends the game.
 f.receiveError=netErrOutOfPackets; udp_sync(0,0); assert(!udp_recv(&link,&from,data) && !udp_error()[0]);
 f.receiveError=99; udp_sync(0,0); assert(!udp_recv(&link,&from,data) && !udp_error()[0]);
 f.receiveError=0; f.sendError=netErrOutOfMemory; assert(udp_send(&link,0,hello,sizeof(hello)) && !udp_error()[0]);
 f.sendError=99; assert(udp_send(&link,0,hello,sizeof(hello)) && !udp_error()[0]); f.sendError=0;
 f.receiveError=netErrSocketNotOpen; udp_sync(0,0); assert(!udp_recv(&link,&from,data) && udp_error()[0]);
 udp_close(); assert(f.closes==1 && f.sockCloses==1);
 reset(); assert(!udp_open(&link,1)); f.sendError=netErrInterfaceDown; assert(udp_send(&link,0,hello,sizeof(hello)) && udp_error()[0]); udp_close();

 // A joiner takes a packet from its host, after a transient error and strangers.
 reset(); assert(!udp_open(&link,1)); assert(!udp_send(&link,0,hello,sizeof(hello)));
 memcpy(&session,f.packet+12,4); session=ntohl(session); assert(session);
 n=wire(packet,0x1234,session,seats,sizeof(seats));
 script(netErrOutOfPackets,NULL,0,0,0);
 script(0,packet,n,0xc0a80109,5050);               // another address
 script(0,packet,n,0xc0a80102,5051);               // another port
 script(0,packet,n-1,0xc0a80102,5050);             // cut short
 memset(big,0,sizeof(big)); memcpy(big,packet,n);
 script(0,big,sizeof(big),0xc0a80102,5050);        // longer than any packet
 script(0,packet,n,0xc0a80102,5050); f.rx[f.rxCount-1].alen=4;       // no full address
 script(0,packet,n,0xc0a80102,5050); f.rx[f.rxCount-1].family=9;     // not IPv4
 n=wire(packet,0x1234,session+1,seats,sizeof(seats));
 script(0,packet,n,0xc0a80102,5050);               // another session
 udp_sync(0,0); sent=f.sent;
 assert(!udp_recv(&link,&from,data) && !udp_error()[0] && f.rxNext==1);
 assert(!udp_recv(&link,&from,data) && f.rxNext==8 && !udp_error()[0] && f.sent==sent);
 f.rxCount=f.rxNext=0; n=wire(packet,0x1234,session,seats,sizeof(seats));
 script(0,packet,n,0xc0a80102,5050);
 from=9; udp_sync(0,0); assert(udp_recv(&link,&from,data)==sizeof(seats) && !from && !memcmp(data,seats,sizeof(seats)));
 // Once the host session is known, another one from the same address is ignored.
 n=wire(packet,0x4321,session,seats,sizeof(seats)); script(0,packet,n,0xc0a80102,5050);
 assert(!udp_recv(&link,&from,data) && f.rxNext==2 && !udp_error()[0]);
 udp_close();

 // A host seats a HELLO, and answers one it can't seat with a reject.
 reset(); assert(!udp_open(&link,0)); udp_sync(1,0);
 n=wire(packet,0,0x77,hello,sizeof(hello)); script(0,packet,n,0xc0a80102,5050);
 sent=f.sent; assert(udp_recv(&link,&from,data)==sizeof(hello) && from==1 && !memcmp(data,hello,sizeof(hello)) && f.sent==sent);
 assert(!udp_send(&link,1,seats,sizeof(seats)) && f.packetLen==UDP_HEADER+sizeof(seats) && !memcmp(f.packet+UDP_HEADER,seats,sizeof(seats)));
 udp_sync(0,2); n=wire(packet,0,0x78,hello,sizeof(hello)); script(0,packet,n,0xc0a80102,5050);
 sent=f.sent; assert(!udp_recv(&link,&from,data) && f.sent==sent+1 && !udp_error()[0]);
 assert(f.packetLen==UDP_HEADER+15 && f.packet[UDP_HEADER]==9 && f.packet[UDP_HEADER+1]==5);
 memcpy(&session,f.packet+12,4); assert(ntohl(session)==0x78);
 udp_close();
 puts("NetLib adapter: open-count ownership, partial failures, cleanup, nonblocking bounds, oversized packets, unreachable peers, retry, transient and fatal errors, scripted receive, wrong address and session, reject reply passed");
 return 0;
}
