// Actual main.c transition handlers, extracted at build time, with Palm UI calls.
#include <assert.h>
#include "savestub.h"
#include "../src/resource.h"
#define NET_PLAY 3
#define NET_STOPPED 4
#define LOG(x) ((void)0)
static GameState gGame;
static struct { UInt16 ticksPerSecond; } gDev={100};
static Boolean gNet,gInGame,gFormOpen,gClockReset,gDirty,gFull,gKeepSave;
static UInt8 gBuild,gFire;
static Int8 gSight;
static UInt16 gForm;
static int answers[4], shown[8], numShown, nextAnswer, closes, saves, exists, allowed, result, destination, connected, polls;
static void ViewMessage(const Char *s) { (void)s; }
static void KeysStop(void) {}
static void KeysStart(UInt16 t) { assert(t==100); }
static void SoundStop(void) {}
static void ViewClose(void) {}
static void FrmCloseAllForms(void) {}
static void ScreenExit(void) {}
static void FrmGotoForm(UInt16 f) { destination=f; }
Boolean SaveExists(void) { return exists; }
void SaveAllow(Boolean b) { allowed=b; }
Boolean SaveAllowed(void) { return allowed; }
Boolean SaveWrite(const GameState *g) { (void)g; assert(allowed); saves++; return true; }
static UInt16 FrmAlert(UInt16 id) {
 assert(numShown<8); shown[numShown++]=id;
 if(id!=LeaveNetAlert) assert(!connected);
 return answers[nextAnswer++];
}
UInt16 FrmCustomAlert(UInt16 id,const Char *a,const Char *b,const Char *c) {
 (void)a;(void)b;(void)c; return FrmAlert(id);
}
static void NetEnd(void) { closes++; connected=0; }
static UInt8 NetOffline(GameState *g) { NetEnd(); if(result==2)g->over=1; return result; }
static UInt8 NetPoll(void) { return polls; }
static Boolean NetLine(const GameState *g,Char *s) { (void)g;(void)s; return false; }
static const Char *NetWhy(void) { return "Host lost"; }
#include "../build/offline-ui.inc"
static void reset(void) {
 memset(&gGame,0,sizeof(gGame)); memset(answers,0,sizeof(answers)); memset(shown,0,sizeof(shown));
 gNet=gInGame=gFormOpen=connected=allowed=1; gForm=GameForm;
 gClockReset=gDirty=gFull=0; gBuild=BUILD_TREE; gFire=5; gSight=1;
 nextAnswer=numShown=closes=saves=exists=destination=0; result=1; polls=NET_STOPPED;
}
int main(void) {
 reset(); answers[0]=1; LeaveGame();
 assert(gNet && gInGame && connected && !closes && !saves && shown[0]==LeaveNetAlert);
 reset(); LeaveGame();
 assert(!gNet && gInGame && !connected && gClockReset && !gBuild && !gFire && !gSight && gDirty && gFull);
 assert(numShown==2 && shown[1]==OfflineAlert); LeaveGame(); assert(saves==1 && destination==TitleForm);
 reset(); exists=1; answers[2]=1; LeaveGame();
 assert(gInGame && !allowed && shown[2]==OfflineSaveAlert); LeaveGame(); assert(!saves && destination==TitleForm);
 reset(); exists=1; answers[2]=1; LeaveGame(); AppStop(); assert(!saves && numShown==3);
 reset(); exists=1; LeaveGame(); assert(gKeepSave); AppStop(); assert(allowed && saves==1);
 reset(); answers[1]=1; LeaveGame(); assert(!gInGame && !saves && destination==TitleForm); AppStop(); assert(!saves);
 reset(); assert(!NetTurns()); assert(!gNet && gInGame && shown[0]==OfflineAlert);
 reset(); result=0; assert(!NetTurns()); assert(!gInGame && destination==TitleForm && shown[0]==ViewAlert);
 reset(); result=2; assert(!NetTurns()); assert(gInGame && gGame.over && gNet && !numShown);
 reset(); AppStop(); assert(!numShown && !connected && saves==1);
 reset(); exists=1; AppStop(); assert(!numShown && !connected && !saves);
 reset(); result=0; AppStop(); assert(!numShown && !saves);
 reset(); result=2; AppStop(); assert(!numShown && !saves);
 puts("offline UI: leave/cancel, loss/error/end, replacement/decline, input reset and app-stop paths pass");
 return 0;
}
