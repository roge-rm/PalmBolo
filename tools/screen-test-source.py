#!/usr/bin/env python3
"""Compile the production screen backend against controlled Palm/Sony calls."""
from pathlib import Path
import re
p = Path('src')
header = (p / 'palmbolo.h').read_text()
device = re.search(r'typedef struct DeviceInfo \{.*?\} DeviceInfo;', header, re.S).group()
sony = (p / 'sonyhr.h').read_text()
sony = re.sub(r'SYS_TRAP\([^)]*\)', '', sony)
draw = (p / 'screendraw.h').read_text()
draw = '\n'.join(l for l in draw.splitlines() if not l.startswith('#'))
body = Path('host/screentest.c').read_text()
# Production source is included verbatim except its SDK includes.
def source(name):
    return '\n'.join(l for l in (p / name).read_text().splitlines() if not l.startswith('#include'))
protos = re.findall(r'extern (.+?) (HRWin\w+)\((.*?)\)\s*;', sony)
stubs=[]
for typ, name, args in protos:
    if name == 'HRWinScreenMode': continue
    plain=name[2:]
    normalargs=args.replace('UInt16 refNum, ', '')
    if plain == 'WinSetClip': normalargs='const RectangleType *rP'
    for api, arglist, value in ((name,args,1),(plain,normalargs,0)):
        code=f'{typ} {api}({arglist}) {{ lastSony={value}; calls++; '
        if value: code+='assert(refNum==7); '
        if plain=='WinGetDisplayExtent': code+=f'*extentX=*extentY={320 if value else 160}; '
        if plain=='WinCreateOffscreenWindow': code+='lastW=width; lastH=height; lastFormat=format; *error=0; '
        if plain=='WinCopyRectangle': code+='lastRect=*srcRect; lastX=destX; lastY=destY; '
        if plain=='WinSetClip':
            code+='lastRect=*rP; '
            if value: code+='assert(!((rP->topLeft.x | rP->topLeft.y | rP->extent.x | rP->extent.y) & 1)); '
        if typ != 'void': code+=f'return ({typ})1; '
        code+='}\n'
        stubs.append(code)
# Declarations before ScreenInit and wrappers; SDK mocks before source.
setup, tests=body.split('/* PRODUCTION */')
text=setup+'\n'+device+'\n'+sony+'\n'+draw+'\n'
text+='Boolean ScreenSony(void); Boolean ScreenSonyNative(void); UInt16 ScreenSonyRef(void); void ScreenNative(Boolean);\n'
text+='HRFontID HRFntSetFont(UInt16 ref,HRFontID f) { assert(ref==7); return (HRFontID)FntSetFont(f); }\n'
text+=''.join(stubs)+'\n'+source('detect.c')+'\n'+source('screen.c')+'\n'+source('screendraw.c')
# Test the same pen mapping used by ViewTap, including panel and buttons.
tile=(p/'tileview.c').read_text(); tap=tile[tile.index('Int16 SEC_UI ViewTap('):tile.index('static void SEC_UI Erase(',tile.index('Int16 SEC_UI ViewTap('))]
panel=(p/'panel.c').read_text()
patch=panel[panel.index('static void SEC_UI OvPatch('):panel.index('void SEC_UI PanelSquare(')]
text+="""
#define kOv 128
static WinHandle gOv=(void*)1;
static struct {void *bits;} gOvB;
static UInt16 gFore;
static RectangleType dirtyRect;
static int painted;
static WinHandle WinSetDrawWindow(WinHandle w) {return w;}
static void OvRowDirect(Coord y,Coord x,Coord end) {(void)y;(void)x;(void)end;assert(0);}
static int BlockClass(Coord x,Coord y) {(void)x;(void)y;return 0;}
static int PixelIndex(int c,Coord x,Coord y) {(void)x;(void)y;return c;}
static void Plot(Coord x,Coord y,int c) {(void)c;assert(x>=0 && x<128 && y>=0 && y<128);painted++;}
static void OvItems(void) {}
static void Dirty(Coord x,Coord y,Coord right,Coord bottom) {
 dirtyRect.topLeft.x=x;dirtyRect.topLeft.y=y;dirtyRect.extent.x=right-x;dirtyRect.extent.y=bottom-y;
}
#define WinSetClip ScreenWinSetClip
static void WinResetClip(void) {}
"""+patch
text+='\nstatic Coord gTs,gW,gH,gBarY; static Int32 gVx,gVy;\n#define kButtons 7\n#define kTapPanel 7\nstatic int covered; static Boolean PanelCovers(void) {return covered;}\n'\
'static Boolean gEdit,gZoom; static UInt8 gEditButtons;\n'\
'static Int16 ZoomTap(Int32 nx,Int32 ny,UInt8 *mx,UInt8 *my) {(void)nx;(void)ny;(void)mx;(void)my;assert(0);return 0;}\n'+tap+tests
Path('build/screen-test.c').write_text(text)
