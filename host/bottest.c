// Focused boat, maze and bounded planner checks using the production rules.
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/game/bot.c"

static GameState g;
static GameInput input[GAME_MAX_TANKS];
static void setup(uint8_t x, uint8_t y, uint8_t gx, uint8_t gy)
{
 GameSetup cfg={0};
 memset(&g,0,sizeof(g)); memset(input,0,sizeof(input)); mapstore_open(TERRAIN_GRASS);
 g.map.numStarts=1; g.map.starts[0].x=x; g.map.starts[0].y=y; g.map.starts[0].active=1;
 g.map.numBases=1; g.map.bases[0].x=gx; g.map.bases[0].y=gy; g.map.bases[0].active=1;
 g.map.bases[0].owner=MAP_NEUTRAL; g.map.bases[0].armour=90; g.map.bases[0].shells=90;
 cfg.numPlayers=1; cfg.hiddenMines=0; game_init(&g,12345,&cfg);
 g.tanks[0].x=mid(x); g.tanks[0].y=mid(y); g.tanks[0].boat=0; g.tanks[0].speed=0;
 g.tanks[0].angle=2048; g.tanks[0].trees=40; g.tanks[0].armour=40; g.tanks[0].shells=40;
 g.men[0].inTank=1; g.men[0].isDead=0; g.men[0].action=LGM_IDLE; g.men[0].nextAction=LGM_IDLE;
 game_set_bot(&g,0,BOT_NORMAL);
}
static void plan(uint8_t x,uint8_t y)
{
 int ticks=0;
 set_goal(&g.bots[0],BG_PATROL,0,x,y); search_begin(&g,0);
 while(g.search.who) { search_step(&g,12); assert(++ticks<400); }
}
static uint16_t at(uint8_t x,uint8_t y) {return (uint16_t)(x-g.search.ox+(y-g.search.oy)*BOT_WIN);}
// A window of known mines, 24 a step, with a winding lane into an open
// half, so costs run far past 2047 (the f a heap key holds without a
// rebase). Every distance must match a plain Dijkstra.
static uint8_t mine_open(int lx,int ly)
{
 return lx>=0&&lx<BOT_WIN&&ly>=0&&ly<BOT_WIN&&mapstore_get(g.search.ox+lx,g.search.oy+ly)==TERRAIN_MINE_GRASS;
}
static void mined_window(void)
{
 static uint16_t ref[BOT_CELLS]; static uint8_t done[BOT_CELLS];
 int x,y,d,i,ticks=0; uint16_t most=0;
 setup(100,100,131,131); mapstore_fill(TERRAIN_BUILDING);
 // Lanes on even rows 100-114 joined at alternate ends, then rows 116-131 open.
 for(y=100;y<=131;y++)for(x=100;x<=131;x++)
  if(y>=116||!(y&1)||(((y-101)/2)&1?x==100:x==131))mapstore_set(x,y,TERRAIN_MINE_GRASS);
 mapstore_set(100,100,TERRAIN_GRASS); mapstore_set(131,131,TERRAIN_BUILDING);
 set_goal(&g.bots[0],BG_PATROL,0,131,131); search_begin(&g,0);
 assert(g.search.ox==100&&g.search.oy==100);
 while(g.search.who){search_step(&g,12);assert(++ticks<2000);}
 for(i=0;i<BOT_CELLS;i++){ref[i]=BOT_NO_CELL;done[i]=0;}
 ref[0]=0;
 for(;;){
  int b=-1;
  for(i=0;i<BOT_CELLS;i++)if(!done[i]&&ref[i]!=BOT_NO_CELL&&(b<0||ref[i]<ref[b]))b=i;
  if(b<0)break;
  done[b]=1;
  for(d=0;d<8;d++){
   int nx=(b&31)+kDX[d],ny=(b>>5)+kDY[d];
   if(!mine_open(nx,ny))continue;
   if((d&1)&&(!mine_open(b&31,ny)||!mine_open(nx,b>>5)))continue;
   if(ref[b]+MINE_COST<ref[nx+ny*BOT_WIN])ref[nx+ny*BOT_WIN]=(uint16_t)(ref[b]+MINE_COST);
  }
 }
 for(i=0;i<BOT_CELLS;i++){
  assert(g.search.distance[i]==ref[i]);
  if(ref[i]!=BOT_NO_CELL&&ref[i]>most)most=ref[i];
 }
 assert(most>4096);
}
static void planner(void)
{
 int x,y; uint16_t n;
 setup(100,100,106,100);
 for(x=101;x<106;x++)mapstore_set(x,100,TERRAIN_FOREST);
 plan(106,100); assert(g.search.distance[at(106,100)]==6);
 // Mine plus hostile zone is 32, with no bucket clamp.
 setup(100,100,101,100); mapstore_fill(TERRAIN_BUILDING);
 mapstore_set(100,100,TERRAIN_GRASS); mapstore_set(101,100,TERRAIN_MINE_GRASS);
 set_goal(&g.bots[0],BG_PATROL,0,101,100); search_begin(&g,0);
 n=at(101,100); g.search.danger[n>>3]|=1<<(n&7);
 while(g.search.who)search_step(&g,12);
 assert(g.search.distance[n]==32);
 // Exhaust the entire window, including beyond the old 600-cell cutoff.
 setup(100,100,104,100); mapstore_set(104,100,TERRAIN_BUILDING); plan(104,100);
 n=0; for(x=0;x<BOT_CELLS;x++)n+=g.search.distance[x]!=BOT_NO_CELL;
 assert(n==BOT_CELLS-1);
 // Shallow shoreline escape is allowed only from an already unsafe square.
 setup(100,100,103,101);mapstore_fill(TERRAIN_BUILDING);
 for(x=100;x<=103;x++) {
  mapstore_set(x,99,TERRAIN_DEEP_SEA);mapstore_set(x,100,TERRAIN_RIVER);
 }
 mapstore_set(102,101,TERRAIN_GRASS);mapstore_set(103,101,TERRAIN_GRASS);
 assert(!step_cost(&g,101,100,0,0) && step_cost(&g,101,100,0,1));
 assert(!step_cost(&g,101,99,0,1));
 plan(103,101);assert(g.bots[0].wayN && g.search.distance[at(103,101)]!=BOT_NO_CELL);
 // Every heap slot is available and ties are stable.
 memset(&g.search,0,sizeof(g.search));g.search.near=255;
 for(x=0;x<BOT_CELLS;x++){g.search.distance[x]=1000+x;heap_up(&g.search,x);}
 g.search.distance[999]=0;heap_up(&g.search,999);assert(heap_pop(&g.search)==999);
 for(x=0;x<BOT_CELLS;x++)if(x!=999)assert(heap_pop(&g.search)==x);
 assert(!g.search.count);
 // A live obstruction invalidates a kept route before driving into it.
 setup(100,100,106,100); plan(106,100);g.bots[0].boatTask=2;
 {uint8_t nx=g.bots[0].way[0][0],ny=g.bots[0].way[0][1];next_square(&g,&g.tanks[0],&nx,&ny);mapstore_set(nx,ny,TERRAIN_BUILDING);}
 navigate(&g,0,&input[0]); assert(g.bots[0].wayN==0 && !g.bots[0].boatTask && g.bots[0].want && !(input[0].keys&IN_FASTER));
 mined_window();
 (void)y;
 puts("planner: cheaper detour, exact hazard cost, full window, heap decrease, mined window and changed obstacle pass");
}
static uint32_t run_capture(const char *name,int limit,int *boarded,int *landed)
{
 int i; uint8_t boat=0;
 for(i=0;i<limit && g.map.bases[0].owner!=0;i++) {
  if(getenv("BOT_TRACE") && i%100==0)fprintf(stderr,"%s %d at %u,%u speed%d boat%u task%u goal%u way%u next%u,%u search%u/%u man%u/%u trees%u\n",name,i,g.tanks[0].x>>8,g.tanks[0].y>>8,g.tanks[0].speed,g.tanks[0].boat,g.bots[0].boatTask,g.bots[0].goal,g.bots[0].wayN,g.bots[0].way[0][0],g.bots[0].way[0][1],g.search.who,g.search.phase,g.men[0].inTank,g.men[0].state,g.tanks[0].trees);
  game_tick(&g,input);
  assert(!g.tanks[0].destroyed && !g.tanks[0].deathWait);
  if(g.tanks[0].boat && !boat)(*boarded)++;
  if(!g.tanks[0].boat && boat)(*landed)++;
  boat=g.tanks[0].boat;
 }
 printf("%s: %d ticks, boarded %d landed %d\n",name,i,*boarded,*landed); fflush(stdout);
 assert(g.map.bases[0].owner==0);
 return game_checksum(&g)^game_terrain_checksum();
}
// Runs a goal the bot can't reach: the base stays neutral and is dropped.
// With stop it ends at the drop, else it runs on to the limit.
static void run_drop(const char *name,int limit,int stop,int *boarded,int *landed)
{
 int i,dropped=-1; uint8_t boat=0;
 for(i=0;i<limit && !(stop && dropped>=0);i++) {
  game_tick(&g,input);
  assert(!g.tanks[0].destroyed && !g.tanks[0].deathWait);
  if(g.tanks[0].boat && !boat)(*boarded)++;
  if(!g.tanks[0].boat && boat)(*landed)++;
  boat=g.tanks[0].boat;
  if(dropped<0 && g.bots[0].skip==((BK_BASE<<5)|0))dropped=i;
 }
 printf("%s: dropped at %d ticks, boarded %d landed %d\n",name,dropped,*boarded,*landed); fflush(stdout);
 assert(g.map.bases[0].owner==MAP_NEUTRAL && dropped>=0);
}
static void sea(int existing)
{
 int x,y;
 setup(97,100,110,100);
 for(y=MAP_EDGE_LOW+1;y<MAP_EDGE_HIGH;y++)for(x=101;x<=105;x++)
  mapstore_set(x,y,x==101||x==105?TERRAIN_RIVER:TERRAIN_DEEP_SEA);
 if(existing)mapstore_set(101,100,TERRAIN_BOAT);
}
static void scenarios(void)
{
 int b=0,l=0,x,y,repeat;uint32_t sums[2];
 setup(100,100,108,100);g.map.bases[0].owner=0;
 g.tanks[0].armour=8;g.tanks[0].shells=2;
 for(x=0;x<3000 && g.tanks[0].armour<40;x++)game_tick(&g,input);
 assert(g.tanks[0].armour==40 && !g.tanks[0].deaths);
 assert((g.tanks[0].x>>8)==108 && (g.tanks[0].y>>8)==100);
 assert(wdist(g.tanks[0].x,g.tanks[0].y,mid(108),mid(100))<=64);
 printf("return to base and refill: %d ticks\n",x);
 setup(100,100,103,101);mapstore_fill(TERRAIN_BUILDING);
 for(x=100;x<=103;x++) {
  mapstore_set(x,99,TERRAIN_DEEP_SEA);mapstore_set(x,100,TERRAIN_RIVER);
 }
 mapstore_set(102,101,TERRAIN_GRASS);mapstore_set(103,101,TERRAIN_GRASS);
 run_capture("follow shallow shoreline escape",4000,&b,&l);assert(!b && !l);
 // A base beside deep sea is taken by land, whatever water is about.
 b=l=0;setup(100,100,110,100);mapstore_set(111,100,TERRAIN_DEEP_SEA);mapstore_set(104,104,TERRAIN_RIVER);
 run_capture("coastal base by land past a pond",2000,&b,&l);assert(!b && !l && g.tanks[0].trees==40);
 // A walled base is dropped without a boat on a pond that leads nowhere.
 b=l=0;setup(100,100,110,100);
 for(x=0;x<8;x++)mapstore_set(110+kDX[x],100+kDY[x],TERRAIN_BUILDING);
 mapstore_set(104,104,TERRAIN_RIVER);
 run_drop("walled base with a pond",6000,0,&b,&l);assert(!b && !l && g.tanks[0].trees==40);
 // A launch nearer than land gets is tried, then the base is dropped from the boat.
 b=l=0;setup(100,100,110,100);
 for(y=96;y<=104;y++)for(x=106;x<=114;x++)mapstore_set(x,y,TERRAIN_BUILDING);
 mapstore_set(106,100,TERRAIN_RIVER);mapstore_set(107,100,TERRAIN_DEEP_SEA);
 run_drop("walled base behind a dead-end launch",6000,1,&b,&l);assert(b==1 && !l);
 for(repeat=0;repeat<2;repeat++) {
  b=l=0;sea(1);sums[repeat]=run_capture("board, cross and land",7000,&b,&l);assert(b==1 && l==1);
 }
 assert(sums[0]==sums[1]);
 // Landing straight off deep sea.
 b=l=0;sea(1);for(y=MAP_EDGE_LOW+1;y<MAP_EDGE_HIGH;y++)mapstore_set(105,y,TERRAIN_DEEP_SEA);
 run_capture("board, cross and land from deep sea",7000,&b,&l);assert(b==1 && l==1);
 b=l=0;sea(0);run_capture("build, cross and land",9000,&b,&l);assert(b==1 && l==1);
 b=l=0;sea(0);g.tanks[0].trees=8;
 mapstore_set(98,99,TERRAIN_FOREST);mapstore_set(99,99,TERRAIN_FOREST);mapstore_set(100,99,TERRAIN_FOREST);
 run_capture("gather wood then build and cross",9000,&b,&l);assert(b==1 && l==1);
 b=l=0;setup(100,100,104,100);
 for(y=90;y<=110;y++)mapstore_set(102,y,TERRAIN_BUILDING);
 for(x=98;x<=106;x++)mapstore_set(x,90,TERRAIN_BUILDING);
 run_capture("maze detour away from goal",9000,&b,&l);
 b=l=0;setup(90,90,118,118);
 for(y=88;y<120;y++)for(x=88;x<120;x++)mapstore_set(x,y,TERRAIN_BUILDING);
 for(y=90;y<=118;y+=2) {
  for(x=90;x<=118;x++)mapstore_set(x,y,TERRAIN_GRASS);
  if(y<118)mapstore_set(((y-90)/2)&1?90:118,y+1,TERRAIN_GRASS);
 }
 run_capture("long maze with more than eight turns",24000,&b,&l);
 // A launch beyond the waypoint ring must keep following the maze.
 for(repeat=0;repeat<2;repeat++) {
  b=l=0;setup(90,90,120,118);
  for(y=88;y<122;y++)for(x=88;x<122;x++)mapstore_set(x,y,TERRAIN_BUILDING);
  for(y=90;y<=118;y+=2) {
   for(x=90;x<=116;x++)mapstore_set(x,y,TERRAIN_GRASS);
   if(y<118)mapstore_set(((y-90)/2)&1?90:116,y+1,TERRAIN_GRASS);
  }
  mapstore_set(117,118,repeat?TERRAIN_BOAT:TERRAIN_RIVER);
  mapstore_set(118,118,TERRAIN_DEEP_SEA);mapstore_set(119,118,TERRAIN_RIVER);
  mapstore_set(120,118,TERRAIN_GRASS);mapstore_set(120,117,TERRAIN_GRASS);
  run_capture(repeat?"long maze then board and land":"long maze then build and land",24000,&b,&l);
  assert(b==1 && l==1 && g.men[0].inTank);
 }
 setup(110,100,120,100);
 g.men[0].inTank=0;g.men[0].state=LGM_STATE_RETURN;g.men[0].obstructed=2;
 g.men[0].x=mid(100);g.men[0].y=mid(100);
 for(y=96;y<=104;y++)mapstore_set(105,y,TERRAIN_BUILDING);
 for(x=0;x<9000 && !g.men[0].inTank;x++)game_tick(&g,input);
 printf("builder recovery: %d ticks\n",x);assert(g.men[0].inTank);
 sea(0);g.tanks[0].x=mid(105);g.tanks[0].boat=1;
 g.men[0].inTank=0;g.men[0].state=LGM_STATE_RETURN;g.men[0].obstructed=2;
 g.men[0].x=mid(99);g.men[0].y=mid(100);
 for(x=0;x<9000 && !g.men[0].inTank;x++)game_tick(&g,input);
 printf("builder across water: %d ticks\n",x);assert(g.men[0].inTank && !g.tanks[0].destroyed);

}
// A live neutral pill at x, y in slot i.
static void pill(uint8_t i,uint8_t x,uint8_t y)
{
 MapPill *p=&g.map.pills[i];
 memset(p,0,sizeof(*p));p->x=x;p->y=y;p->owner=MAP_NEUTRAL;p->armour=R_PILL_MAX_ARMOUR;
 p->speed=p->reload=R_PILL_ATTACK_NORMAL;p->active=1;
 if(g.map.numPills<=i)g.map.numPills=(uint8_t)(i+1);
}
// An own base at x, y in slot i.
static void put_base(uint8_t i,uint8_t x,uint8_t y)
{
 MapBase *b=&g.map.bases[i];
 memset(b,0,sizeof(*b));b->x=x;b->y=y;b->owner=0;b->armour=90;b->shells=90;b->active=1;
 if(g.map.numBases<=i)g.map.numBases=(uint8_t)(i+1);
}
typedef struct Fire { int ticks,entries[2],onHot,maxStay,minArmour,live,heat,deaths,hits,filled,idle,took; } Fire;
// Runs the bot by bases under pill fire. Counts the times it drives onto
// each base from more than a square away, the ticks it is on one that pills
// reach and the longest such stay, its deaths, the armour it takes off
// pills, the armour and shells bases give it, the pills and bases it takes
// and the longest it stays on one square with one goal. It must not die
// filling up on a base. With stop 1 it ends when the tank is full, with 2 when no hostile
// pill is left.
static int hostile_left(void)
{
 int i,n=0;
 for(i=0;i<g.map.numPills;i++)n+=pill_hostile(&g,&g.map.pills[i],0);
 return n;
}
static Fire run_fire(const char *name,int limit,int stop)
{
 Fire f={0};int i,j,stay=0,still=0,away[2]={1,1},dead=0;
 uint8_t pa[MAP_MAX_PILLS],po[MAP_MAX_PILLS],bo[MAP_MAX_BASES],lx=0,ly=0,lg=0,lt=0;
 f.minArmour=40;
 for(i=0;i<limit && !(stop==1 && g.tanks[0].armour==40 && g.tanks[0].shells>=36) && !(stop==2 && !hostile_left());i++) {
  int on,arm=g.tanks[0].armour,sh=g.tanks[0].shells,now;
  uint8_t tx,ty;
  for(j=0;j<g.map.numPills;j++){pa[j]=g.map.pills[j].armour;po[j]=g.map.pills[j].owner;}
  for(j=0;j<g.map.numBases;j++)bo[j]=g.map.bases[j].owner;
  if(getenv("BOT_TRACE") && i%50==0)fprintf(stderr,"%s %d at %u,%u arm %u sh %u goal %u tgt %u to %u,%u mode %u pills %u/%u speed %u/%u base %u/%u\n",name,i,g.tanks[0].x>>8,g.tanks[0].y>>8,g.tanks[0].armour,g.tanks[0].shells,g.bots[0].goal,g.bots[0].target,g.bots[0].goalX,g.bots[0].goalY,g.bots[0].mode,g.map.pills[0].armour,g.map.pills[1].armour,g.map.pills[0].speed,g.map.pills[1].speed,g.map.bases[0].armour,g.map.bases[0].shells);
  game_tick(&g,input);
  tx=(uint8_t)(g.tanks[0].x>>8);ty=(uint8_t)(g.tanks[0].y>>8);
  on=g_base_at(&g,tx,ty);
  now=g.tanks[0].destroyed || g.tanks[0].deathWait;
  if(now && !dead) {
   f.deaths++;
   if(on>=0 && lg==BG_REFUEL) {
    printf("%s: died filling up on base %d at tick %d\n",name,on,i);fflush(stdout);
    assert(!"bot died filling up on a base under pill fire");
   }
  }
  dead=now;
  if(dead){still=0;continue;}
  for(j=0;j<g.map.numPills;j++) {
   if(g.map.pills[j].armour<pa[j])f.hits+=pa[j]-g.map.pills[j].armour;
   if(g.map.pills[j].owner!=po[j])f.took++;
  }
  for(j=0;j<g.map.numBases;j++)if(g.map.bases[j].owner!=bo[j])f.took++;
  if(on>=0 && g.tanks[0].armour>arm)f.filled+=g.tanks[0].armour-arm;
  if(on>=0 && g.tanks[0].shells>sh)f.filled+=g.tanks[0].shells-sh;
  for(j=0;j<2 && j<g.map.numBases;j++) {
   if(on==j && away[j])f.entries[j]++;
   away[j]=on==j?0:away[j] || cheb(tx,ty,g.map.bases[j].x,g.map.bases[j].y)>1;
  }
  if(on>=0 && pill_heat(&g,0,g.map.bases[on].x,g.map.bases[on].y)) {
   f.onHot++;if(++stay>f.maxStay)f.maxStay=stay;
  } else stay=0;
  // Idle: one square and one goal, and nothing coming from a base.
  if(tx==lx && ty==ly && g.bots[0].goal==lg && g.bots[0].target==lt && g.tanks[0].armour<=arm && g.tanks[0].shells<=sh) {
   if(++still>f.idle)f.idle=still;
  } else still=0;
  lx=tx;ly=ty;lg=g.bots[0].goal;lt=g.bots[0].target;
  if(g.tanks[0].armour<f.minArmour)f.minArmour=g.tanks[0].armour;
 }
 f.ticks=i;
 for(i=0;i<g.map.numPills;i++)f.live+=pill_hostile(&g,&g.map.pills[i],0) && g.map.pills[i].armour>0;
 f.heat=pill_heat(&g,0,(uint8_t)(g.tanks[0].x>>8),(uint8_t)(g.tanks[0].y>>8));
 printf("%s: %d ticks, base entries %d and %d, %d ticks on a base under fire (longest %d), least armour %d, armour %u shells %u, live pills %d, heat where it is %d, deaths %d, pill armour shot off %d, filled %d, took %d, longest idle %d\n",
  name,f.ticks,f.entries[0],f.entries[1],f.onHot,f.maxStay,f.minArmour,g.tanks[0].armour,g.tanks[0].shells,f.live,f.heat,f.deaths,f.hits,f.filled,f.took,f.idle);
 fflush(stdout);
 return f;
}
static void under_fire(void)
{
 Fire f;
 // Its only base under two neutral pills, low on armour with shells.
 setup(100,100,104,100);put_base(0,104,100);pill(0,104,95);pill(1,108,103);
 g.tanks[0].armour=15;
 f=run_fire("only base under two pills, with shells",20000,2);
 // Cornered, it runs at the pills anyway: it shoots them, never waits long
 // and gets somewhere, by taking a pill, filling up or coming back full.
 assert(f.hits>0 && (f.took || f.filled || f.deaths) && f.idle<=1500 && f.entries[0]<=4);
 // Two bases, the nearer one under a pill: it fills up at the other.
 setup(96,104,103,100);put_base(0,103,100);put_base(1,96,116);pill(0,106,98);
 g.tanks[0].armour=8;g.tanks[0].shells=2;
 f=run_fire("two bases, the nearer under a pill",6000,1);
 assert(g.tanks[0].armour==40 && g.tanks[0].shells>=36);
 assert(!f.entries[0] && f.entries[1]==1 && !f.onHot && f.minArmour==8);
 assert((g.tanks[0].x>>8)==96 && (g.tanks[0].y>>8)==116);
 // Its only base under two neutral pills and no shells.
 setup(100,100,104,100);put_base(0,104,100);pill(0,104,95);pill(1,108,103);
 g.tanks[0].armour=15;g.tanks[0].shells=0;
 f=run_fire("only base under two pills, no shells",20000,2);
 // With nothing else to do it takes shells from the base, leaves before the
 // base runs dry under it, and then runs at the pills.
 assert(f.entries[0]>=1 && f.entries[0]<=4 && f.maxStay<=1200 && f.filled>0);
 assert(f.hits>0 && f.idle<=1500);
 // Its only base under one pill at rest and no shells: a few visits for
 // shells, then runs at the pill.
 setup(100,100,104,100);put_base(0,104,100);pill(0,104,95);
 g.tanks[0].shells=0;
 f=run_fire("only base under one pill, no shells",20000,2);
 assert(f.entries[0]>=1 && f.entries[0]<=6 && f.maxStay<=1200 && f.filled>=30 && f.hits>0 && f.idle<=1500);
}
// A full tank whose only base is under fire still has a base: it shoots
// the pill guarding a neutral base before taking it.
static void guarded(void)
{
 int i;
 setup(100,100,103,100);put_base(1,100,140);pill(0,100,143);pill(1,97,138);pill(2,106,100);
 for(i=0;i<60 && g.bots[0].goal==BG_NONE;i++)game_tick(&g,input);
 printf("guarded neutral base with its own base under fire: goal %u target %u\n",g.bots[0].goal,g.bots[0].target);
 assert(fuel_base(&g,0)<0 && own_base(&g,0)==1);
 assert(g.bots[0].goal==BG_PILL && g.bots[0].target==2);
}
// Saved while the builder is out to build a boat and resumed after another
// game: it plays on the same as the game that wasn't saved.
static void save_boat(void)
{
 static GameState saved;
 int i,at=-1,b=0,l=0;uint32_t straight;
 sea(0);
 for(i=0;i<9000 && at<0;i++) {
  game_tick(&g,input);
  if(g.bots[0].boatTask==2 && !g.men[0].inTank && g.men[0].state==LGM_STATE_GOING)at=i;
 }
 assert(at>=0);
 memcpy(&saved,&g,sizeof(g));assert(mapstore_save()==0);
 straight=run_capture("boat build, played straight",9000,&b,&l);
 sea(1);for(i=0;i<300;i++)game_tick(&g,input);
 assert(mapstore_resume()==0);memcpy(&g,&saved,sizeof(g));
 b=l=0;
 assert(run_capture("boat build, saved and resumed",9000,&b,&l)==straight);
 mapstore_drop_save();
 printf("save during a boat build at tick %d: resumed game matches\n",at);
}
int main(void) {planner();scenarios();under_fire();guarded();save_boat();printf("bot checks pass; GameState %zu, BotSearch %zu, Bot %zu bytes on host\n",sizeof(GameState),sizeof(BotSearch),sizeof(Bot));return 0;}
