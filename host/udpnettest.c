// Exercise the production Palm adapter with controlled NetLib results.
#include <assert.h>
#include "netstub/NetMgr.h"
#include "../src/net/udp.h"
static struct {
 int missing, count, increment, openError, interfaceError, socketError, optionError, bindError;
 int closes, sockets, sockCloses, receiveError, sendError, receiveCount, oversized;
 UInt16 boundPort;
 UInt8 packet[UDP_PACKET]; UInt16 packetLen;
} f;
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
{ NetSocketAddrINType *a=address; assert(ref==7 && socket==3 && !flags && !timeout && alen==sizeof(*a)); assert(ntohl(a->addr)==0xc0a80102 && ntohs(a->port)==5050); *err=f.sendError; if (*err) return -1; memcpy(f.packet,data,len); f.packetLen=len; return len; }
Int16 NetLibReceive(UInt16 ref, NetSocketRef socket, void *data, UInt16 len, UInt16 flags, void *address, UInt16 *alen, Int32 timeout, Err *err)
{ assert(ref==7 && socket==3 && len==UDP_PACKET+1 && !flags && !timeout && *alen==sizeof(NetSocketAddrINType)); (void)data;(void)address; f.receiveCount++; if (f.oversized) {f.oversized--; *err=netErrMessageTooBig;} else *err=f.receiveError?f.receiveError:netErrWouldBlock; return -1; }
Err NetLibIFGet(UInt16 ref, UInt16 index, UInt32 *creator, UInt16 *instance)
{ assert(ref==7); *creator=1; *instance=0; return index?99:0; }
Err NetLibIFSettingGet(UInt16 ref, UInt32 creator, UInt16 instance, UInt16 setting, void *value, UInt16 *len)
{ assert(ref==7 && creator==1 && instance==0 && setting==netIFSettingActualIPAddr && *len==4); *(UInt32 *)value=htonl(0xc0a80103); return 0; }
Char *NetLibAddrINToA(UInt16 ref, UInt32 ip, Char *out)
{ assert(ref==7 && ntohl(ip)==0xc0a80103); strcpy(out,"192.168.1.3"); return out; }
static void reset(void) { udp_close(); memset(&f,0,sizeof(f)); f.increment=1; udp_config(0xc0a80102,5050); }
int main(void)
{
 Link link; char text[48]; uint8_t from, data[64], hello[]={1,1,'A'};
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
 f.receiveError=99; udp_sync(0,0); assert(!udp_recv(&link,&from,data) && udp_error()[0]);
 udp_close(); assert(f.closes==1 && f.sockCloses==1);
 reset(); assert(!udp_open(&link,1)); f.sendError=99; assert(udp_send(&link,0,hello,sizeof(hello)) && udp_error()[0]); udp_close();
 puts("NetLib adapter: open-count ownership, partial failures, cleanup, nonblocking bounds, oversized packets, unreachable peers, retry and fatal errors passed");
 return 0;
}
