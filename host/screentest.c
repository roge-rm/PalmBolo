#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
typedef uint8_t UInt8; typedef uint16_t UInt16; typedef uint32_t UInt32;
typedef int16_t Int16; typedef int32_t Int32; typedef char Char;
typedef bool Boolean; typedef int16_t Coord; typedef unsigned char FontID;
typedef unsigned char IndexedColorType; typedef int Err;
typedef struct { Coord x,y; } PointType;
typedef struct { PointType topLeft,extent; } RectangleType;
typedef void *WinHandle; typedef struct {int unused;} BitmapType; typedef BitmapType *BitmapPtr;
typedef int WindowFormatType; typedef int WinDrawOperation; typedef int WinDirectionType;
typedef int WinScreenModeOperation;
typedef struct { UInt8 index,r,g,b; } RGBColorType;
#define SEC_UI
#define SEC_COLD
#define SEC_DRAW
#define LOG(x) ((void)0)
#define sysMakeROMVersion(a,b,c,d,e) ((a)*100+(b)*10)
#define sysROMStageRelease 0
#define kRom35 350
#define kMinRomVersion 300
#define kDensityDouble 144
#define errNone 0
#define sysErrLibNotFound 1
#define screenFormat 0
#define nativeFormat 1
#define winScreenModeGet 0
#define winScreenModeSet 1
static int found,loadError,openError,versionError,modeError,closeError;
static int opens,closes,loads,removes,modeSets,lastSony,calls,lastFormat;
static UInt16 apiVersion=0x100;
static UInt32 modeW=160,modeH=160,depth=4;
static FontID font;
static Coord lastW,lastH,lastX,lastY;
static RectangleType lastRect;
static Err SysLibFind(const Char *name, UInt16 *ref) {
 assert(!strcmp(name,"Sony HR Library")); *ref=7; return found?0:sysErrLibNotFound;
}
static Err SysLibLoad(UInt32 type, UInt32 creator, UInt16 *ref) {
 assert(type=='libr' && creator=='SlHr'); loads++; *ref=7; return loadError;
}
static Err SysLibRemove(UInt16 ref) { assert(ref==7); removes++; return 0; }
Err HROpen(UInt16 ref) { assert(ref==7); opens++; return openError; }
Err HRClose(UInt16 ref) { assert(ref==7); closes++; return closeError; }
Err HRGetAPIVersion(UInt16 ref, UInt16 *v) { assert(ref==7); *v=apiVersion; return versionError; }
Err HRWinScreenMode(UInt16 ref, WinScreenModeOperation op, UInt32 *w,UInt32 *h,UInt32 *d,Boolean *c) {
 assert(ref==7);
 if(op==winScreenModeGet) { *w=modeW; *h=modeH; *d=depth; *c=true; }
 else {modeSets++; if(modeError)return modeError; if(w)modeW=*w; if(h)modeH=*h; if(d)depth=*d;}
 return 0;
}
static Err WinScreenMode(WinScreenModeOperation op, UInt32 *w,UInt32 *h,UInt32 *d,Boolean *c) {
 (void)w;(void)h;(void)c; if(op==0)*d=depth; else if(op==2)*d=128; else if(op==3)*c=true; else depth=*d; return 0;
}
static FontID FntGetFont(void) { return font; }
static FontID FntSetFont(FontID f) { FontID old=font; font=f; return old; }

static IndexedColorType WinRGBToIndex(const RGBColorType *c) { return c->r; }
#define SEC_DETECT
#define sysFtrCreator 1
#define sysFtrNumROMVersion 1
#define sysFtrNumProcessorID 2
#define sysFtrNumWinVersion 3
#define pinCreator 2
#define pinFtrAPIVersion 1
#define kDensityLow 72
#define winScreenDensity 1
#define winScreenModeGetSupportedDepths 2
#define winScreenModeGetSupportsColor 3
#define MemSet(p,n,v) memset(p,v,n)
static UInt32 testRom=400,testWinVersion,testDensity=144;
static Boolean testSony=true;
static UInt16 MemHeapID(UInt16 c,UInt16 h) {(void)c;return h;}
static void MemHeapFreeBytes(UInt16 h,UInt32 *f,UInt32 *m) {(void)h;*f=*m=65536;}
static UInt32 MemHeapSize(UInt16 h) {(void)h;return 65536;}
static UInt16 SysTicksPerSecond(void) {return 100;}
static Err FtrGet(UInt32 creator,UInt16 number,UInt32 *v) {
 if(creator==sysFtrCreator) { *v=number==sysFtrNumROMVersion?testRom:number==sysFtrNumWinVersion?testWinVersion:0;return 0;}
 if(creator=='SoNy' && testSony) {*v=1;return 0;} return 1;
}
static Err WinScreenGetAttribute(UInt32 a,UInt32 *v) {assert(a==winScreenDensity);*v=testDensity;return 0;}
static Int16 FntLineHeight(void) {return font>=8?22:11;}
static Int16 FntCharsWidth(const Char *s,Int16 n) {(void)s;return n*(font>=8?10:5);}
/* PRODUCTION */
static void reset(void) {
 found=1; loadError=openError=versionError=modeError=closeError=0;
 opens=closes=loads=removes=modeSets=calls=0; modeW=modeH=160; depth=4; font=0;
}
int main(void) {
 DeviceInfo d={0}; RectangleType r={{3,5},{256,42}}; UInt16 err; Coord x,y;
 UInt8 mx,my; int i;
 d.rom=400; d.sony=true; d.depthMask=128;
 reset(); DeviceDetect(&d); assert(d.sony && d.sonyHR && d.density==72); ScreenInit(&d); assert(ScreenSony() && ScreenHiRes() && opens==1 && depth==8 && modeW==320);
 ScreenWinGetDisplayExtent(&x,&y); assert(x==160 && !lastSony);
 ScreenNative(true); assert(font==8 && FntLineHeight()==22 && FntCharsWidth("40",2)==20);
 ScreenWinGetDisplayExtent(&x,&y); assert(x==320 && lastSony);
 ScreenFont(1); assert(font==9); ScreenFont(0); assert(font==8);
 ScreenWinCreateOffscreenWindow(256,48,nativeFormat,&err); assert(lastSony && lastW==256 && lastH==48 && lastFormat==screenFormat);
 ScreenAlignRect(&r);
 assert(r.topLeft.x==2 && r.topLeft.y==4 && r.extent.x==258 && r.extent.y==44);
 ScreenWinSetClip(&r); assert(lastSony && lastRect.extent.x==258 && lastRect.topLeft.x==2);
 { int px,py,size;
   for(px=0;px<128;px++)for(py=0;py<128;py++)for(size=1;size<=5;size+=4) {
    RectangleType a={{px,py},{size,size}};
    ScreenAlignRect(&a); ScreenWinSetClip(&a);
    assert(a.topLeft.x<=px && a.topLeft.y<=py);
    assert(a.topLeft.x+a.extent.x>=px+size && a.topLeft.y+a.extent.y>=py+size);
   }
 }
 ScreenWinCopyRectangle((void*)1,(void*)2,&r,17,21,0); assert(lastSony && lastX==17 && lastY==21 && lastRect.extent.y==44);
 { int px,py,size;
   for(px=-2;px<128;px++)for(py=-2;py<128;py++)for(size=1;size<=5;size+=4) {
    if(px+size<=0 || py+size<=0)continue;
    painted=0; OvPatch(px,py,size,size);
    assert(painted==dirtyRect.extent.x*dirtyRect.extent.y);
    assert(!memcmp(&lastRect,&dirtyRect,sizeof(lastRect)));
   }
 }
 ScreenNative(false); assert(font==0 && FntLineHeight()==11); ScreenWinDrawLine(1,2,3,4); assert(!lastSony);
 ScreenExit(); assert(modeW==160 && depth==4 && closes==1 && !removes);
 reset(); found=0; ScreenInit(&d); assert(loads==1 && ScreenSony()); ScreenExit(); assert(removes==1);
 reset(); found=0; closeError=3; ScreenInit(&d); ScreenExit(); assert(!removes);
 for(i=0;i<4;i++) {
  reset(); found=0;
  if(i==0)loadError=1;
  if(i==1)openError=1;
  if(i==2)versionError=1;
  if(i==3)modeError=1;
  ScreenInit(&d); assert(!ScreenSony() && !ScreenHiRes()); ScreenExit();
  assert(removes==(i!=0)); assert(closes==(i>=2));
 }
 reset(); testWinVersion=4; DeviceDetect(&d); assert(d.winVersion==4 && d.density==144); ScreenInit(&d); assert(ScreenHiRes() && !ScreenSony() && !opens);
 ScreenNative(true); ScreenWinDrawLine(1,2,3,4); assert(!lastSony); ScreenNative(false); ScreenExit();
 reset(); d.winVersion=0; d.sony=false; d.sonyHR=false; ScreenInit(&d); assert(!ScreenHiRes() && !opens); ScreenExit();
 // Same map square, panel boundary and all seven controls at both scales.
 for(i=1;i<=2;i++) {
  int b; gTs=8*i; gW=160*i; gH=110*i; gBarY=140*i; gVx=80*i; gVy=160*i;
  assert(ViewTap(24,32,&mx,&my)==-1 && mx==13 && my==24);
  assert(ViewTap(24,110,&mx,&my)==kTapPanel);
  for(b=0;b<7;b++)assert(ViewTap((b*160+80)/7,150,&mx,&my)==b);
  covered=1; assert(ViewTap(24,32,&mx,&my)==kTapPanel); covered=0;
 }
 puts("screen: Sony lifecycle/failure/ownership, OS5 and160 fallbacks, native dispatch, offscreen geometry, fonts, clips, copies and pen mapping pass");
 return 0;
}
