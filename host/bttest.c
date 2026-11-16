// Four production adapters on a controlled asynchronous BtLib, with real lockstep.
#include <assert.h>
#include <stdlib.h>
#include <BtLib.h>
#include "../src/net/bluetooth.h"
#include "../src/net/lockstep.h"
#include "../src/game/mapstore.h"
#include "../src/game/util.h"
#define DECL(n) \
 int bt##n##_available(void); int bt##n##_open(Link *,uint8_t); void bt##n##_close(void); \
 int bt##n##_send(Link *,uint8_t,const uint8_t *,uint8_t); \
 uint8_t bt##n##_recv(Link *,uint8_t *,uint8_t *); \
 void bt##n##_sync(uint8_t,uint8_t); void bt##n##_service(void); const char *bt##n##_error(void);
DECL(0) DECL(1) DECL(2) DECL(3) DECL(4)
typedef struct {
 int (*available)(void); int (*open)(Link *,uint8_t); void (*close)(void);
 int (*send)(Link *,uint8_t,const uint8_t *,uint8_t); uint8_t (*recv)(Link *,uint8_t *,uint8_t *);
 void (*sync)(uint8_t,uint8_t); void (*service)(void); const char *(*error)(void);
} Api;
#define API(n) {bt##n##_available,bt##n##_open,bt##n##_close,bt##n##_send,bt##n##_recv,bt##n##_sync,bt##n##_service,bt##n##_error}
static Api api[]={API(0),API(1),API(2),API(3),API(4)};
#define SOCKS 80
#define EVENTS 256
static unsigned current, now, busyEvery, sends, refusals, completions;
static int immediate, holdEvents, syncConnect, uiEvents, keyEvents, rejectBeforeAccept;
static EventType uiQueue[16], keyQueue[16];
static struct Device {
 Link link; Lockstep ls; GameState game; int open,ready,gone,mgmt,record,advert,pico,acl,disconnects;
 int missing, openError, regError, picoError, discoverError, aclError, createError, listenError, recordError, advertError, queryError, connectError, sendError;
 BtLibManagementProcPtr callback; UInt32 epoch;
 uint8_t map[65535]; uint16_t have;
} dev[5];
static struct Socket {
 int used,owner,peer,listener,pending,protocol;
 BtLibSocketProcPtr callback; UInt32 epoch;
 UInt8 *buffer; UInt32 len;
} sockets[SOCKS];
static struct Event {
 int used,owner,management; UInt32 epoch;
 BtLibSocketProcPtr callback; BtLibManagementProcPtr mgmt;
 BtLibSocketEventType socket; BtLibManagementEventType m;
 UInt8 bytes[64]; UInt8 *sendPtr; unsigned delay;
} events[EVENTS];
static unsigned head,tail;
static uint8_t source[65535]; static uint16_t sourceLen;
void mapstore_host_bank(int n);
static BtLibDeviceAddressType address(int n) { BtLibDeviceAddressType a={{0}};a.address[5]=n+1;return a; }
static void ref(UInt16 r) { assert(r==7 && dev[current].open); }
Err SysLibFind(const Char *s,UInt16 *r) { assert(!strcmp(s,btLibName));*r=7;return dev[current].missing; }
Err FtrGet(UInt32 c,UInt16 n,UInt32 *v) { assert(c==btLibFeatureCreator && n==0);*v=1;return dev[current].missing; }
UInt32 TimGetTicks(void) { return now; }
UInt16 SysTicksPerSecond(void) { return 1000; }
void SysTaskDelay(Int32 t) { now+=t; }
static struct Event *event(void) { struct Event *e=&events[tail++%EVENTS];assert(!e->used);memset(e,0,sizeof(*e));e->used=1;return e; }
static void se(int s,int kind,Err status) {
 struct Event *e=event();e->owner=sockets[s].owner;e->callback=sockets[s].callback;e->epoch=sockets[s].epoch;
 e->socket.event=kind;e->socket.socket=s;e->socket.status=status;
}
static void me(int who,int kind,Err status,int about) {
 struct Event *e=event();e->owner=who;e->management=1;e->mgmt=dev[who].callback;e->epoch=dev[who].epoch;
 e->m.event=kind;e->m.status=status;e->m.eventData.bdAddr=address(about);
}
static void deliver(void) {
 unsigned saved=current, limit=tail;
 if(holdEvents)return;
 while(head<limit) {
  struct Event e=events[head%EVENTS];events[head++%EVENTS].used=0;current=e.owner;
  if(e.management) { if(e.mgmt)e.mgmt(&e.m,e.epoch); }
  else { if(e.socket.event==btLibSocketEventData)e.socket.eventData.data.data=e.bytes;
   if(e.socket.event==btLibSocketEventSendComplete) {
    assert(!memcmp(e.sendPtr,e.bytes,e.socket.eventData.data.dataLen));completions++;
   }
   if(e.callback)e.callback(&e.socket,e.epoch);
  }
 }
 current=saved;
}
void EvtGetEvent(EventType *e,Int32 timeout) { assert(!timeout);memset(e,0,sizeof(*e));if(uiEvents){*e=uiQueue[0];memmove(uiQueue,uiQueue+1,--uiEvents*sizeof(*e));} }
Err EvtAddEventToQueue(const EventType *e) { assert(uiEvents<16);uiQueue[uiEvents++]=*e;return 0; }
Err EvtDequeueKeyEvent(SysEventType *e,UInt16 peek) {
 memset(e,0,sizeof(*e));
 if(keyEvents){*e=keyQueue[0];if(!peek)memmove(keyQueue,keyQueue+1,--keyEvents*sizeof(*e));return 0;}
 if(!holdEvents&&head<tail){e->eType=keyDownEvent;e->data.keyDown.modifiers=libEvtHookKeyMask;return 0;}
 return 1;
}
Boolean SysHandleEvent(EventType *e) { assert(e->eType==keyDownEvent);deliver();return true; }
Err BtLibOpen(UInt16 r,Boolean allow) { assert(r==7&&!allow);if(dev[current].openError && dev[current].openError!=btLibErrAlreadyOpen)return dev[current].openError;dev[current].open++;return dev[current].openError; }
Err BtLibClose(UInt16 r) {ref(r);dev[current].open--;return 0;}
Err BtLibRegisterManagementNotification(UInt16 r,BtLibManagementProcPtr cb,UInt32 epoch) {ref(r);if(dev[current].regError)return dev[current].regError;dev[current].callback=cb;dev[current].epoch=epoch;dev[current].mgmt++;return 0;}
Err BtLibUnregisterManagementNotification(UInt16 r,BtLibManagementProcPtr cb) {ref(r);assert(dev[current].callback==cb&&dev[current].mgmt==1);dev[current].mgmt--;dev[current].callback=NULL;return 0;}
Err BtLibPiconetCreate(UInt16 r,Boolean inbound,Boolean discoverable) {ref(r);assert(inbound&&discoverable);if(dev[current].picoError)return dev[current].picoError;dev[current].pico++;if(syncConnect)return 0;me(current,btLibManagementEventPiconetCreated,0,0);return btLibErrPending;}
Err BtLibPiconetDestroy(UInt16 r) {ref(r);assert(dev[current].pico==1);dev[current].pico--;return 0;}
Err BtLibDiscoverSingleDevice(UInt16 r,Char *label,BtLibClassOfDeviceType *filter,UInt8 n,BtLibDeviceAddressType *out,Boolean addr,Boolean previous) {ref(r);assert(label&&!filter&&!n&&!addr&&!previous);*out=address(0);return dev[current].discoverError;}
Err BtLibLinkConnect(UInt16 r,BtLibDeviceAddressType *a) {ref(r);assert(a->address[5]==1);if(dev[current].aclError)return dev[current].aclError;dev[current].acl++;if(syncConnect)return 0;me(current,btLibManagementEventACLConnectOutbound,0,0);return btLibErrPending;}
// Each end of a dropped ACL link hears about the other device.
Err BtLibLinkDisconnect(UInt16 r,BtLibDeviceAddressType *a) {ref(r);if(!current){assert(a->address[5]>1);dev[current].disconnects++;me(a->address[5]-1,btLibManagementEventACLDisconnect,0,0);return 0;}assert(a->address[5]==1&&dev[current].acl);dev[current].acl--;dev[current].disconnects++;me(0,btLibManagementEventACLDisconnect,0,current);return 0;}
static int socket_new(int owner,BtLibSocketProcPtr cb,UInt32 epoch,int protocol) {int s;for(s=1;s<SOCKS&&sockets[s].used;s++);assert(s<SOCKS);memset(&sockets[s],0,sizeof(sockets[s]));sockets[s].used=1;sockets[s].owner=owner;sockets[s].callback=cb;sockets[s].epoch=epoch;sockets[s].protocol=protocol;return s;}
Err BtLibSocketCreate(UInt16 r,BtLibSocketRef *s,BtLibSocketProcPtr cb,UInt32 epoch,BtLibProtocolEnum protocol) {ref(r);if(dev[current].createError)return dev[current].createError;*s=socket_new(current,cb,epoch,protocol);return 0;}
Err BtLibSocketClose(UInt16 r,BtLibSocketRef s) {
 ref(r);assert(s>0&&s<SOCKS);if(!sockets[s].used)return btLibErrSocket;assert(sockets[s].owner==(int)current);
 int peer=sockets[s].peer;sockets[s].used=0;
 // Closing a socket cancels its callbacks and invalidates its counterpart.
 for(unsigned j=head;j<tail;j++)if(!events[j%EVENTS].management&&events[j%EVENTS].socket.socket==s)events[j%EVENTS].callback=NULL;
 if(peer&&sockets[peer].used){se(peer,btLibSocketEventDisconnected,btLibErrNoAclLink);sockets[peer].used=0;sockets[peer].peer=0;}
 return 0;
}
Err BtLibSocketListen(UInt16 r,BtLibSocketRef s,BtLibSocketListenInfoType *info) {ref(r);assert(sockets[s].protocol==btLibL2CapProtocol&&info->data.L2Cap.localPsm==0xffff&&info->data.L2Cap.localMtu==64&&info->data.L2Cap.minRemoteMtu==64);if(dev[current].listenError)return dev[current].listenError;sockets[s].listener=1;info->data.L2Cap.localPsm=0x1001;return 0;}
Err BtLibSocketConnect(UInt16 r,BtLibSocketRef s,BtLibSocketConnectInfoType *info) {ref(r);assert(info->remoteDeviceP->address[5]==1&&info->data.L2Cap.remotePsm==0x1001&&info->data.L2Cap.localMtu==64&&info->data.L2Cap.minRemoteMtu==64);if(dev[current].connectError)return dev[current].connectError;
 int listener;for(listener=1;listener<SOCKS;listener++)if(sockets[listener].used&&sockets[listener].owner==0&&sockets[listener].listener)break;
 if(listener==SOCKS)return btLibErrNoAclLink;
 assert(!sockets[listener].pending);sockets[listener].pending=s;se(listener,btLibSocketEventConnectRequest,0);events[(tail-1)%EVENTS].socket.eventData.requestingDevice=address(current);return btLibErrPending;}
Err BtLibSocketRespondToConnection(UInt16 r,BtLibSocketRef listener,Boolean accept) {ref(r);int remote=sockets[listener].pending;assert(remote);sockets[listener].pending=0;
 if(!accept){se(listener,btLibSocketEventDisconnected,btLibErrFailed);se(remote,btLibSocketEventConnectedOutbound,btLibErrFailed);return btLibErrPending;}
 int child=socket_new(current,sockets[listener].callback,sockets[listener].epoch,btLibL2CapProtocol);
 sockets[child].peer=remote;sockets[remote].peer=child;
 // An unrelated rejection can finish before this accepted connection.
 if(rejectBeforeAccept){se(listener,btLibSocketEventDisconnected,btLibErrFailed);rejectBeforeAccept=0;}
 se(listener,btLibSocketEventConnectedInbound,0);events[(tail-1)%EVENTS].socket.eventData.newSocket=child;se(remote,btLibSocketEventConnectedOutbound,0);return btLibErrPending;}
Err BtLibSocketSend(UInt16 r,BtLibSocketRef s,UInt8 *data,UInt32 n) {ref(r);assert(n&&n<=64&&sockets[s].owner==(int)current);if(dev[current].sendError)return dev[current].sendError;
 if(busyEvery&&++sends%busyEvery==0){refusals++;return btLibErrBusy;}
 int peer=sockets[s].peer;if(!peer||!sockets[peer].used)return btLibErrNoAclLink;
 se(peer,btLibSocketEventData,0);struct Event *e=&events[(tail-1)%EVENTS];memcpy(e->bytes,data,n);e->socket.eventData.data.dataLen=n;
 if(immediate)return 0;
 se(s,btLibSocketEventSendComplete,0);e=&events[(tail-1)%EVENTS];memcpy(e->bytes,data,n);e->sendPtr=data;e->socket.eventData.data.data=data;e->socket.eventData.data.dataLen=n;return btLibErrPending;}
Err BtLibSdpGetPsmByUuid(UInt16 r,BtLibSocketRef s,BtLibDeviceAddressType *a,BtLibSdpUuidType *u,UInt8 n) {ref(r);assert(a->address[5]==1&&u->size==16&&n==1);if(dev[current].queryError)return dev[current].queryError;se(s,btLibSocketEventSdpGetPsmByUuid,dev[0].advert?0:btLibErrFailed);events[(tail-1)%EVENTS].socket.eventData.sdpByUuid.param.psm=0x1001;return btLibErrPending;}
Err BtLibSdpServiceRecordCreate(UInt16 r,BtLibSdpRecordHandle *record) {ref(r);if(dev[current].recordError)return dev[current].recordError;dev[current].record++;*record=&dev[current];return 0;}
Err BtLibSdpServiceRecordDestroy(UInt16 r,BtLibSdpRecordHandle record) {ref(r);assert(record==&dev[current]&&dev[current].record==1&&!dev[current].advert);dev[current].record--;return 0;}
Err BtLibSdpServiceRecordStartAdvertising(UInt16 r,BtLibSdpRecordHandle record) {ref(r);assert(record==&dev[current]);if(dev[current].advertError)return dev[current].advertError;dev[current].advert++;return 0;}
Err BtLibSdpServiceRecordStopAdvertising(UInt16 r,BtLibSdpRecordHandle record) {ref(r);assert(record==&dev[current]&&dev[current].advert==1);dev[current].advert--;return 0;}
Err BtLibSdpServiceRecordSetAttributesForSocket(UInt16 r,BtLibSocketRef s,BtLibSdpUuidType *u,UInt8 n,const Char *name,UInt16 len,BtLibSdpRecordHandle record) {ref(r);assert(sockets[s].listener&&u->size==16&&n==1&&len==8&&!strcmp(name,"PalmBolo")&&record==&dev[current]);return 0;}

int link_send(Link *l,uint8_t to,const uint8_t *data,uint8_t len){current=l->end;return api[current].send(l,to,data,len);}
uint8_t link_recv(Link *l,uint8_t *from,uint8_t *data){current=l->end;return api[current].recv(l,from,data);}
static void sync_dev(int i) {current=i;uint8_t mask=0;for(int j=1;j<8;j++)if(dev[i].ls.seated&(1<<j))mask|=1<<dev[i].ls.node[j];api[i].sync(i==0&&dev[i].ls.state==LS_LOBBY,mask);}
static void poll_all(void) {deliver();for(int i=0;i<5;i++)if(dev[i].open&&!dev[i].gone){sync_dev(i);ls_poll(&dev[i].ls,now);sync_dev(i);}}
static void open_dev(int i) {current=i;memset(&dev[i].ls,0,sizeof(dev[i].ls));dev[i].link.end=i;dev[i].ready=dev[i].gone=dev[i].have=0;assert(!api[i].open(&dev[i].link,i?LINK_ROLE_JOIN:LINK_ROLE_HOST));if(i)ls_join_open(&dev[i].ls,&dev[i].link,"Guest");else ls_host_open(&dev[i].ls,&dev[i].link,"Host");}
static void advance(unsigned duration) {unsigned end=now+duration;for(;now<end;now+=10)poll_all();}
static void close_all(void) {holdEvents=0;for(int i=4;i>=0;i--){current=i;api[i].close();}deliver();for(int i=0;i<5;i++)assert(!dev[i].open&&!dev[i].record&&!dev[i].advert&&!dev[i].mgmt&&!dev[i].pico&&!dev[i].acl);for(int i=1;i<SOCKS;i++)assert(!sockets[i].used);}
static void reset(void) {close_all();memset(dev,0,sizeof(dev));memset(events,0,sizeof(events));memset(sockets,0,sizeof(sockets));head=tail=now=sends=busyEvery=refusals=completions=0;immediate=syncConnect=uiEvents=keyEvents=rejectBeforeAccept=0;}
static void get_map(void *ctx,uint16_t off,uint8_t *dst,uint8_t n){(void)ctx;memcpy(dst,source+off,n);}
static uint8_t put_map(void *ctx,uint16_t off,const uint8_t *src,uint8_t n){struct Device *d=ctx;assert(off==d->have&&off+n<=sourceLen);memcpy(d->map+off,src,n);d->have+=n;return 0;}
static void setup(int i) {MapSource input;MapMemory memory;GameSetup gs;mapstore_host_bank(i);mapstore_open(TERRAIN_DEEP_SEA);map_source_memory(&input,&memory,source,sourceLen);assert(map_parse(&dev[i].game.map,&input)==MAP_OK);ls_setup(&dev[i].ls.start,&gs);game_init(&dev[i].game,dev[i].ls.start.seed,&gs);dev[i].ready=1;}
static void integration(void) {
 reset();FILE *f=fopen("res/everard.map","rb");assert(f);sourceLen=fread(source,1,sizeof(source),f);fclose(f);
 open_dev(0);ls_host_lobby(&dev[0].ls,"Everard Island",0,0,get_map,NULL);advance(100);
 for(int i=1;i<4;i++){open_dev(i);advance(500);}assert(dev[0].ls.seated==15);
 open_dev(4);advance(500);current=4;assert(api[4].error()[0]);api[4].close();
 current=3;ls_leave(&dev[3].ls);advance(300);assert(dev[0].ls.seated==7&&dev[0].disconnects);current=3;api[3].close();deliver();open_dev(3);advance(500);assert(dev[0].ls.seated==15);
 LsStart start;memset(&start,0,sizeof(start));start.seed=71;start.mapLen=sourceLen;start.mapSum=util_crc(0xffffffffUL,source,sourceLen)^0xffffffffUL;start.delay=3;start.dropSecs=3;start.sumTurns=16;
 current=0;ls_host_start(&dev[0].ls,&start);setup(0);sync_dev(0);busyEvery=19;
 open_dev(4);int left=0;
 for(;now<90000;now+=10){
  poll_all();
  for(int i=1;i<4;i++)if(!dev[i].ready&&dev[i].ls.state==LS_STARTING){if(dev[i].ls.fetch==LS_MAP_NONE)ls_join_fetch(&dev[i].ls,put_map,&dev[i]);if(dev[i].ls.fetch==LS_MAP_GOT){assert(dev[i].have==sourceLen&&!memcmp(dev[i].map,source,sourceLen));setup(i);ls_join_ready(&dev[i].ls);}}
  if(!left&&dev[0].game.tick>=512){current=3;api[3].close();dev[3].gone=1;left=1;}
  for(int i=0;i<4;i++)if(dev[i].ready&&!dev[i].gone&&dev[i].game.tick<1024&&ls_ready(&dev[i].ls)){GameInput input;memset(&input,0,sizeof(input));input.keys=dev[i].game.tick%40<20?IN_FASTER|IN_LEFT:IN_FIRE;mapstore_host_bank(i);ls_tick(&dev[i].ls,&dev[i].game,&input);}
  if(dev[0].game.tick==1024&&dev[1].game.tick==1024&&dev[2].game.tick==1024)break;
 }
 assert(now<90000&&left);current=4;assert(api[4].error()[0]);
 uint32_t sum=game_checksum(&dev[0].game);mapstore_host_bank(0);uint32_t terrain=game_terrain_checksum();
 for(int i=0;i<3;i++){assert(dev[i].ls.state==LS_PLAYING&&dev[i].ls.sumsCompared);assert(game_checksum(&dev[i].game)==sum);mapstore_host_bank(i);assert(game_terrain_checksum()==terrain);}
 current=0;ls_leave(&dev[0].ls);advance(1000);assert(dev[1].ls.stop==LS_STOP_HOST_LEFT&&dev[2].ls.stop==LS_STOP_HOST_LEFT);
 printf("Bluetooth four adapters: map transfer, 1024 matching ticks, full/late rejection, lobby reuse, dropped peer/bot takeover, host leave; %u busy sends, %u completions\n",refusals,completions);close_all();
}
static void lifecycle(void) {
 reset();current=0;dev[0].missing=1;assert(!api[0].available()&&api[0].open(&dev[0].link,0));dev[0].missing=0;
 dev[0].openError=btLibErrFailed;assert(api[0].open(&dev[0].link,0)&&!dev[0].open);dev[0].openError=btLibErrAlreadyOpen;assert(api[0].open(&dev[0].link,0)&&!dev[0].open);dev[0].open=1;assert(api[0].open(&dev[0].link,0)&&dev[0].open==1);dev[0].open=0;dev[0].openError=0;
 dev[0].regError=btLibErrFailed;assert(api[0].open(&dev[0].link,0)&&!dev[0].open);dev[0].regError=0;
 dev[0].picoError=btLibErrFailed;assert(api[0].open(&dev[0].link,0)&&!dev[0].open);dev[0].picoError=0;
 current=1;dev[1].discoverError=btLibErrCanceled;assert(api[1].open(&dev[1].link,1)&&!dev[1].open);dev[1].discoverError=0;
 dev[1].aclError=btLibErrAlreadyConnected;assert(!api[1].open(&dev[1].link,1));api[1].close();assert(!dev[1].disconnects);dev[1].aclError=0;
 // A cancelled pending connection cannot advance a reopened session.
 assert(!api[1].open(&dev[1].link,1));BtLibManagementProcPtr stale=dev[1].callback;UInt32 epoch=dev[1].epoch;api[1].close();deliver();assert(!api[1].open(&dev[1].link,1));
 BtLibManagementEventType m;memset(&m,0,sizeof(m));m.event=btLibManagementEventRadioState;m.status=btLibErrRadioFatal;stale(&m,epoch);assert(!api[1].error()[0]);api[1].close();deliver();
 reset();syncConnect=1;open_dev(0);sync_dev(0);assert(dev[0].advert);rejectBeforeAccept=1;open_dev(1);advance(800);assert(dev[0].ls.seated==3);
 // More than one service batch of distinct application events keeps FIFO order.
 current=0;EventType expected[12];memset(expected,0,sizeof(expected));
 for(int i=0;i<12;i++){expected[i].eType=i%2?appStopEvent:penDownEvent;expected[i].data.keyDown.chr=i;EvtAddEventToQueue(&expected[i]);}
 keyEvents=2;memset(keyQueue,0,sizeof(keyQueue));keyQueue[0].eType=keyDownEvent;keyQueue[0].data.keyDown.chr='A';
 keyQueue[1].eType=keyDownEvent;keyQueue[1].data.keyDown.modifiers=libEvtHookKeyMask;
 api[0].service();assert(uiEvents==12&&!memcmp(uiQueue,expected,sizeof(expected))&&keyEvents==2);
 SysEventType key;assert(!EvtDequeueKeyEvent(&key,0)&&key.data.keyDown.chr=='A');api[0].service();
 assert(!keyEvents&&uiEvents==12&&!memcmp(uiQueue,expected,sizeof(expected)));
 for(int i=0;i<12;i++){EventType actual;EvtGetEvent(&actual,0);assert(!memcmp(&actual,&expected[i],sizeof(actual)));}
 uint8_t data[64]={250,1},copy[64],from;current=1;holdEvents=1;assert(!api[1].send(&dev[1].link,0,data,64));data[0]=251;assert(api[1].send(&dev[1].link,0,data,64));holdEvents=0;deliver();current=0;assert(api[0].recv(&dev[0].link,&from,copy)==64&&copy[0]==250&&from==1);
 current=1;immediate=1;assert(!api[1].send(&dev[1].link,0,data,64));assert(!api[1].send(&dev[1].link,0,data,64));deliver();immediate=0;
 // A callback queue overflow drops just that reliable connection.
 for(int i=0;i<9;i++){current=1;assert(!api[1].send(&dev[1].link,0,data,64));deliver();}
 sync_dev(0);assert(!api[0].error()[0]);advance(100);close_all();
 reset();open_dev(0);advance(100);int oldSocket=0;
 for(int s=1;s<SOCKS;s++)if(sockets[s].used&&sockets[s].listener)oldSocket=s;
 assert(oldSocket);BtLibSocketProcPtr oldCallback=sockets[oldSocket].callback;UInt32 oldEpoch=sockets[oldSocket].epoch;
 close_all();open_dev(0);advance(100);current=0;
 BtLibSocketEventType oldEvent;memset(&oldEvent,0,sizeof(oldEvent));oldEvent.socket=oldSocket;
 oldEvent.event=btLibSocketEventConnectRequest;oldEvent.eventData.requestingDevice=address(1);
 oldCallback(&oldEvent,oldEpoch);assert(!api[0].error()[0]);close_all();
 reset();open_dev(0);holdEvents=1;now=31000;sync_dev(0);assert(api[0].error()[0]);close_all();
 for(int fault=0;fault<4;fault++){
  reset();if(fault==0)dev[0].createError=btLibErrFailed;if(fault==1)dev[0].listenError=btLibErrFailed;
  if(fault==2)dev[0].recordError=btLibErrFailed;
  if(fault==3)dev[0].advertError=btLibErrFailed;
  open_dev(0);advance(100);current=0;assert(api[0].error()[0]);close_all();
 }
 for(int fault=0;fault<2;fault++){
  reset();open_dev(0);advance(100);if(fault==0)dev[1].queryError=btLibErrFailed;else dev[1].connectError=btLibErrFailed;
  open_dev(1);advance(500);current=1;assert(api[1].error()[0]);close_all();
 }
 puts("Bluetooth lifecycle: unavailable/busy/radio failure, cancellation, borrowed ACL, stale epoch, pending/synchronous sends, overflow, timeout, SDP/socket failures, input FIFO, overlapping rejection completion and cleanup passed");
}
// A frame sent just before closing still reaches the other side.
static void leave_close(void) {
 uint8_t data[64]={250,7},copy[64],from;
 // The frame and the disconnect are handled back to back before the host reads.
 reset();open_dev(0);ls_host_lobby(&dev[0].ls,"Everard Island",0,0,get_map,NULL);advance(100);
 open_dev(1);advance(800);assert(dev[0].ls.seated==3);
 current=1;assert(!api[1].send(&dev[1].link,0,data,2));api[1].close();dev[1].gone=1;deliver();
 current=0;assert(api[0].recv(&dev[0].link,&from,copy)==2&&from==1&&copy[0]==250&&copy[1]==7);
 assert(!api[0].recv(&dev[0].link,&from,copy));
 // The freed seat takes a new connection with nothing left from the old one.
 advance(4000);assert(dev[0].ls.seated==1);open_dev(1);advance(800);assert(dev[0].ls.seated==3);close_all();
 // A joiner's LEAVE frees its seat at once, not after the silence timeout.
 reset();open_dev(0);ls_host_lobby(&dev[0].ls,"Everard Island",0,0,get_map,NULL);advance(100);
 open_dev(1);advance(500);open_dev(2);advance(500);assert(dev[0].ls.seated==7);
 deliver();current=1;ls_leave(&dev[1].ls);ls_poll(&dev[1].ls,now);api[1].close();dev[1].gone=1;deliver();
 poll_all();assert(dev[0].ls.seated==3);
 // The host's STOP is read before the joiner reports the lost connection.
 deliver();current=0;ls_leave(&dev[0].ls);ls_poll(&dev[0].ls,now);api[0].close();dev[0].gone=1;deliver();
 sync_dev(2);current=2;assert(!api[2].error()[0]);
 poll_all();current=2;assert(dev[2].ls.stop==LS_STOP_HOST_LEFT&&api[2].error()[0]);
 puts("Bluetooth leave then close: last frame, joiner LEAVE and host STOP delivered across the disconnect, seat reuse passed");close_all();
}
int main(void){lifecycle();leave_close();integration();return 0;}
