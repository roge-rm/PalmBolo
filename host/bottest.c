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
 for(x=0;x<BOT_CELLS;x++){g.search.position[x]=BOT_NO_CELL;g.search.distance[x]=1000+x;heap_up(&g.search,x);}
 g.search.distance[999]=0;heap_up(&g.search,999);assert(heap_pop(&g.search)==999);
 for(x=0;x<BOT_CELLS;x++)if(x!=999)assert(heap_pop(&g.search)==x);
 assert(!g.search.count);
 // A live obstruction invalidates a kept route before driving into it.
 setup(100,100,106,100); plan(106,100);g.bots[0].boatTask=2;
 {uint8_t nx=g.bots[0].way[0][0],ny=g.bots[0].way[0][1];next_square(&g,&g.tanks[0],&nx,&ny);mapstore_set(nx,ny,TERRAIN_BUILDING);}
 navigate(&g,0,&input[0]); assert(g.bots[0].wayN==0 && !g.bots[0].boatTask && g.bots[0].want && !(input[0].keys&IN_FASTER));
 (void)y;
 puts("planner: cheaper detour, exact hazard cost, full window, heap decrease and changed obstacle pass");
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
int main(void) {planner();scenarios();printf("bot checks pass; GameState %zu, BotSearch %zu, Bot %zu bytes on host\n",sizeof(GameState),sizeof(BotSearch),sizeof(Bot));return 0;}
