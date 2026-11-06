// Composing the view offscreen, on OS 3.5 and later where the view is
// drawn with the OS calls. Tiles and sprites go into an offscreen window
// the size of the view, and the parts that changed are copied to the
// screen when the frame is done, so a sprite is never seen erased.
// Without the memory for it the view is drawn on screen as before.
#include "palmbolo.h"

#define kMaxFlush   48
// Left free in the dynamic heap after the window is made.
#define kHeapSpare  32768UL

static WinHandle gComp;
static Coord gCw, gCh;
static RectangleType gFlush[kMaxFlush];
static UInt16 gNumFlush;
static Boolean gAll;

void SEC_DRAW CompFree(void)
{
	if (gComp)
		WinDeleteWindow(gComp, false);
	gComp = NULL;
	gCw = gCh = 0;
}

// Makes the window w by h (native pixels while they are on) if it isn't
// that size already. Returns whether there is one.
Boolean SEC_DRAW CompFit(Coord w, Coord h)
{
	UInt32 free = 0, max = 0, need;
	UInt16 err = 0;

	if (gComp && gCw == w && gCh == h)
		return true;
	CompFree();
	if (!ScreenNewWin() || w <= 0 || h <= 0)
		return false;
#ifdef NO_COMPOSE
	return false;
#endif
	need = (UInt32)w * h * ScreenDepth() / 8;
	MemHeapFreeBytes(0, &free, &max);
	if (max < need + kHeapSpare) {
		LOG(("view offscreen: %lu bytes wanted, biggest free %lu: drawn on screen", need, max));
		return false;
	}
	gComp = WinCreateOffscreenWindow(w, h, ScreenHiRes() ? nativeFormat : screenFormat, &err);
	if (!gComp) {
		LOG(("view offscreen: no window (err %x): drawn on screen", err));
		return false;
	}
	gCw = w;
	gCh = h;
	LOG(("view offscreen %d x %d, about %lu bytes", (Int16)w, (Int16)h, need));
	return true;
}

WinHandle SEC_DRAW CompWin(void)
{
	return gComp;
}

void SEC_DRAW CompBegin(void)
{
	gNumFlush = 0;
	gAll = false;
}

// r (view pixels) changed and goes to the screen at the end of the frame.
void SEC_DRAW CompAdd(const RectangleType *r)
{
	if (gAll || r->extent.x <= 0 || r->extent.y <= 0)
		return;
	if (gNumFlush >= kMaxFlush) {
		gAll = true;
		return;
	}
	gFlush[gNumFlush++] = *r;
}

void SEC_DRAW CompAll(void)
{
	gAll = true;
}

// Copies what changed to dst, the view being w by h at its top left.
void SEC_DRAW CompFlush(WinHandle dst)
{
	RectangleType r;
	UInt16 i;

	if (!gComp)
		return;
	WinResetClip();
	if (gAll) {
		r.topLeft.x = r.topLeft.y = 0;
		r.extent.x = gCw;
		r.extent.y = gCh;
		WinCopyRectangle(gComp, dst, &r, 0, 0, winPaint);
		return;
	}
	for (i = 0; i < gNumFlush; i++) {
		r = gFlush[i];
		if (r.topLeft.x < 0) {
			r.extent.x += r.topLeft.x;
			r.topLeft.x = 0;
		}
		if (r.topLeft.y < 0) {
			r.extent.y += r.topLeft.y;
			r.topLeft.y = 0;
		}
		if (r.topLeft.x + r.extent.x > gCw)
			r.extent.x = gCw - r.topLeft.x;
		if (r.topLeft.y + r.extent.y > gCh)
			r.extent.y = gCh - r.topLeft.y;
		if (r.extent.x > 0 && r.extent.y > 0)
			WinCopyRectangle(gComp, dst, &r, r.topLeft.x, r.topLeft.y, winPaint);
	}
}
