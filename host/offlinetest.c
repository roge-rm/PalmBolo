// Production conversion and save code with a small Palm database adapter.
#include <assert.h>
#include "savestub.h"
#include "../src/net/lockstep.h"
#include "../src/game/mapstore.h"

void mapstore_host_bank(int n);
struct Handle { UInt8 *p; UInt32 n; };
struct Database { Char name[32]; UInt32 type; struct Handle rec; Boolean live; } dbs[3];
static int failCreate;
UInt16 DmNumRecords(DmOpenRef d) { return d->rec.p != NULL; }
MemHandle DmQueryRecord(DmOpenRef d, UInt16 r) { return r ? NULL : &d->rec; }
UInt32 MemHandleSize(MemHandle h) { return h->n; }
void *MemHandleLock(MemHandle h) { return h->p; }
Err MemHandleUnlock(MemHandle h) { (void)h; return 0; }
LocalID DmFindDatabase(UInt16 c, const Char *n) {
 int i; (void)c; for(i=0;i<3;i++) if(dbs[i].live && !strcmp(dbs[i].name,n)) return i+1; return 0;
}
Err DmCreateDatabase(UInt16 c,const Char *n,UInt32 cr,UInt32 t,Boolean r) {
 int i; (void)c;(void)cr;(void)r; if(failCreate) return 1;
 for(i=0;i<3;i++) if(!dbs[i].live) { dbs[i].live=1; strcpy(dbs[i].name,n); dbs[i].type=t; return 0; }
 return 1;
}
DmOpenRef DmOpenDatabaseByTypeCreator(UInt32 t,UInt32 cr,UInt16 m) {
 int i; (void)cr;(void)m; for(i=0;i<3;i++) if(dbs[i].live && dbs[i].type==t) return &dbs[i]; return NULL;
}
MemHandle DmNewRecord(DmOpenRef d,UInt16 *r,UInt32 n) { *r=0; d->rec.n=n; d->rec.p=malloc(n); assert(d->rec.p); return &d->rec; }
Err DmWrite(void *p,UInt32 off,const void *b,UInt32 n) { memcpy((UInt8 *)p+off,b,n); return 0; }
Err DmReleaseRecord(DmOpenRef d,UInt16 r,Boolean dirty) { (void)d;(void)r;(void)dirty; return 0; }
Err DmCloseDatabase(DmOpenRef d) { (void)d; return 0; }
Err DmDeleteDatabase(UInt16 c,LocalID id) { (void)c; free(dbs[id-1].rec.p); memset(&dbs[id-1],0,sizeof(dbs[0])); return 0; }
Err DmSetDatabaseInfo(UInt16 c,LocalID id,const Char *n,UInt16 *a,void *b,void *d,void *e,void *f,void *g,void *h,void *i,UInt32 *t,void *j) {
 (void)c;(void)a;(void)b;(void)d;(void)e;(void)f;(void)g;(void)h;(void)i;(void)j;
 strcpy(dbs[id-1].name,n); dbs[id-1].type=*t; return 0;
}
static int protocolMode, helloVersion, rejected;
int link_send(Link *l,uint8_t to,const uint8_t *b,uint8_t n) {
 (void)l;(void)to;assert(protocolMode);
 if(n>=2 && b[0]==9 && b[1]==LS_STOP_VERSION)rejected++;
 return 0;
}
uint8_t link_recv(Link *l,uint8_t *from,uint8_t *b) {
 (void)l;assert(protocolMode);
 if(!helloVersion)return 0;
 *from=1;b[0]=1;b[1]=helloVersion;b[2]=0;helloVersion=0;return 3;
}
static void protocol(void) {
 Lockstep ls;Link link={0};protocolMode=1;
 ls_host_open(&ls,&link,"host");helloVersion=LS_VERSION-1;ls_poll(&ls,1);
 assert(rejected==1 && ls.seated==1);
 helloVersion=LS_VERSION;ls_poll(&ls,2);assert(ls.seated==3 && rejected==1);
 protocolMode=0;puts("protocol: old bot rules refused, current version joins");
}
static void setup(GameState *g, Lockstep *ls, int bank, int slot) {
 GameSetup s = {0};
 mapstore_host_bank(bank); mapstore_open(TERRAIN_GRASS);
 memset(g,0,sizeof(*g)); g->map.numStarts=4;
 for(int i=0;i<4;i++) { g->map.starts[i].x=50+i*8; g->map.starts[i].y=50; g->map.starts[i].active=1; }
 s.numPlayers=3; s.numBots=1; s.botLevel=BOT_HARD; s.hiddenMines=1;
 s.allies[0]=2; s.allies[1]=1;
 game_set_viewer(slot); game_init(g,12345,&s);
 g->tanks[slot].trees=17; g->tanks[slot].shells=23;
 mapstore_set(100,100,TERRAIN_MINE_GRASS); mapstore_set_flag(MS_SEEN,100,100,1);
 memset(ls,0,sizeof(*ls)); ls->slot=slot; ls->state=LS_PLAYING;
 ls->start.botLevel=BOT_HARD; ls->start.delay=2; ls->start.sumTurns=64;
 ls->done[0]=1; ls->act[0]=7;
 for(int i=0;i<3;i++) for(int j=0;j<LS_TICKS;j++) ls->in[0][i].keys[j]=IN_FASTER;
}
static uint32_t terrain(void) {
 uint32_t sum=0; for(int y=0;y<256;y++) for(int x=0;x<256;x++) sum=sum*33+mapstore_get(x,y);
 for(int p=0;p<MS_PLANES;p++) for(int i=0;i<8192;i++) sum=sum*33+mapstore_flags(p)[i];
 return sum;
}
static void conversion(void) {
 GameState g[2], expected; Lockstep ls[2]; GameInput none={0};
 for(int sub=0;sub<4;sub++) for(int slot=0;slot<3;slot++) {
  uint32_t sums[2];
  for(int b=0;b<2;b++) {
   setup(&g[b],&ls[b],b,slot);
   for(int t=0;t<sub;t++) ls_tick(&ls[b],&g[b],&none);
   if(b==0) {
    // Reference: run the rest of the same received turn before conversion.
    while(ls[b].sub) ls_tick(&ls[b],&g[b],&none);
    expected=g[b];
    for(int i=0;i<3;i++) if(i!=slot) game_set_bot(&expected,i,BOT_HARD);
   } else { ls[b].state=LS_STOPPED; ls[b].stop=LS_STOP_HOST_LOST; }
   assert(ls_offline(&ls[b],&g[b])==1);
   assert(game_viewer()==slot && game_viewer_tank(&g[b])==slot);
   assert(!memcmp(&g[b],&expected,sizeof(expected)));
   assert(g[b].tick==(sub?4:0));
   assert(g[b].botMask==(uint8_t)(15 & ~(1<<slot)));
   assert(g[b].players[0].allies==2 && g[b].players[1].allies==1);
   assert(!memcmp(&ls[b].mine,&(LsInput){0},sizeof(LsInput)));
   sums[b]=terrain();
  }
  assert(sums[0]==sums[1]);
  // Subsequent offline ticks remain deterministic, with input in the joiner's slot.
  for(int b=0;b<2;b++) {
   GameInput all[GAME_MAX_TANKS]={{0}};
   mapstore_host_bank(b); game_set_viewer(slot); all[slot].keys=IN_FASTER|IN_FIRE;
   for(int t=0;t<64;t++) game_tick(&g[b],all);
   sums[b]=terrain();
  }
  assert(!memcmp(&g[0],&g[1],sizeof(g[0])) && sums[0]==sums[1]);
 }
 // A checksum arriving before link loss still validates the final buffered turn.
 for(int sub=1;sub<4;sub++) {
  uint32_t sum;
  const int reasons[]={LS_STOP_HOST_LEFT,LS_STOP_HOST_LOST,LS_STOP_DROPPED};
  setup(&g[0],&ls[0],0,2); ls[0].start.sumTurns=1;
  for(int t=0;t<4;t++) ls_tick(&ls[0],&g[0],&none);
  sum=ls_checksum(&g[0]);
  for(unsigned r=0;r<sizeof(reasons)/sizeof(reasons[0]);r++) for(int bad=0;bad<2;bad++) {
   setup(&g[1],&ls[1],1,2); ls[1].start.sumTurns=1;
   for(int t=0;t<sub;t++) ls_tick(&ls[1],&g[1],&none);
   ls[1].state=LS_STOPPED; ls[1].stop=reasons[r];
   ls[1].remWait=1; ls[1].remTick[0]=4; ls[1].remVal[0]=sum^(uint32_t)bad;
   assert(ls_offline(&ls[1],&g[1])==(bad?0:1));
   assert(ls[1].stop==reasons[r] && g[1].tick==4);
   if(bad) assert(g[1].botMask==8);
  }
 }
 // Stop reasons and partial-turn validity.
 for(int reason=LS_STOP_NONE;reason<=LS_STOP_END;reason++) {
  setup(&g[0],&ls[0],0,2); ls[0].state=LS_STOPPED; ls[0].stop=reason; expected=g[0];
  int ok=reason==LS_STOP_NONE || reason==LS_STOP_HOST_LEFT || reason==LS_STOP_HOST_LOST || reason==LS_STOP_DROPPED;
  assert(ls_offline(&ls[0],&g[0])==ok);
  if(!ok) assert(!memcmp(&g[0],&expected,sizeof(expected)));
 }
 setup(&g[0],&ls[0],0,2); ls_tick(&ls[0],&g[0],&none); ls[0].done[0]=0; expected=g[0];
 assert(!ls_offline(&ls[0],&g[0]) && !memcmp(&g[0],&expected,sizeof(expected)));
 setup(&g[0],&ls[0],0,2); ls[0].ended=1; expected=g[0];
 assert(!ls_offline(&ls[0],&g[0]) && !memcmp(&g[0],&expected,sizeof(expected)));
 setup(&g[0],&ls[0],0,2); ls[0].finishing=1;
 assert(!ls_offline(&ls[0],&g[0]));
 setup(&g[0],&ls[0],0,2); g[0].over=1; expected=g[0];
 assert(ls_offline(&ls[0],&g[0])==2 && !memcmp(&g[0],&expected,sizeof(expected)));
 setup(&g[0],&ls[0],0,2); g[0].timeLimit=2; ls_tick(&ls[0],&g[0],&none);
 assert(ls_offline(&ls[0],&g[0])==2 && g[0].over && g[0].tick==2 && g[0].botMask==8);
 setup(&g[0],&ls[0],0,2); game_set_bot(&g[0],2,BOT_HARD); ls[0].stop=LS_STOP_DROPPED;
 assert(ls_offline(&ls[0],&g[0])==1 && !(g[0].botMask&4));
 puts("offline: boundary/partial turns, stopped checksum validation, stop reasons, teams, entities, terrain, viewer and determinism pass");
}
static void saves(void) {
 GameState g, saved, read; Lockstep ls; uint32_t oldTerrain;
 setup(&g,&ls,0,2); assert(ls_offline(&ls,&g)==1); saved=g; oldTerrain=terrain();
 SaveAllow(true); assert(SaveWrite(&g) && SaveExists());
 game_set_viewer(0); memset(&read,0,sizeof(read)); mapstore_fill(TERRAIN_ROAD);
 assert(SaveRead(&read) && !memcmp(&saved,&read,sizeof(saved)));
 assert(game_viewer()==2 && game_viewer_tank(&read)==2 && terrain()==oldTerrain);
 // A declined replacement protects both state and map through later write/drop calls.
 SaveAllow(false); g.tick+=100; mapstore_fill(TERRAIN_FOREST);
 assert(!SaveAllowed() && !SaveWrite(&g)); SaveDrop();
 assert(SaveExists() && SaveRead(&read) && read.tick==saved.tick && terrain()==oldTerrain);
 SaveAllow(true); failCreate=1; assert(!SaveWrite(&g)); failCreate=0;
 assert(SaveRead(&read) && read.tick==saved.tick);
 assert(SaveWrite(&g) && SaveRead(&read) && read.tick==g.tick);
 // Older bot layouts and search ordering are refused without changing the record.
 DmOpenRef d=DmOpenDatabaseByTypeCreator('Sgam','Rmrf',1);
 UInt16 version; UInt32 bytes=d->rec.n; UInt8 *copy=malloc(bytes); assert(copy);
 for(version=2;version<=3;version++) {
  memcpy(d->rec.p+4,&version,2);memcpy(copy,d->rec.p,bytes);
  assert(!SaveExists() && !SaveRead(&read));
  assert(d->live && d->rec.n==bytes && !memcmp(copy,d->rec.p,bytes));
 }
 free(copy);
 version=1; memcpy(d->rec.p+4,&version,2); d->rec.n--;
 assert(!SaveExists() && !SaveRead(&read) && d->live);
 SaveDrop(); assert(!SaveExists());
 puts("save: v4 joiner identity/state/map, older layouts preserved and refused, declined overwrite/drop and allocation failure pass");
}
int main(void) { conversion(); saves(); protocol(); return 0; }
