#include "palmbolo.h"
#include "sonyhr.h"

void SEC_DRAW ScreenWinCopyRectangle(WinHandle srcWin, WinHandle dstWin, RectangleType *srcRect, Coord destX, Coord destY, WinDrawOperation mode)
{
	if (ScreenSonyNative()) {
		HRWinCopyRectangle(ScreenSonyRef(), srcWin, dstWin, srcRect, destX, destY, mode);
		return;
	}
	WinCopyRectangle(srcWin, dstWin, srcRect, destX, destY, mode);
}

WinHandle SEC_DRAW ScreenWinCreateBitmapWindow(BitmapType *bitmapP, UInt16 *error)
{
	if (ScreenSonyNative()) {
		return HRWinCreateBitmapWindow(ScreenSonyRef(), bitmapP, error);
	}
	return WinCreateBitmapWindow(bitmapP, error);
}

WinHandle SEC_DRAW ScreenWinCreateOffscreenWindow(Coord width, Coord height, WindowFormatType format, UInt16 *error)
{
	if (ScreenSonyNative()) {
		return HRWinCreateOffscreenWindow(ScreenSonyRef(), width, height, screenFormat, error);
	}
	return WinCreateOffscreenWindow(width, height, format, error);
}

void SEC_DRAW ScreenWinDrawBitmap(BitmapPtr bitmapP, Coord x, Coord Y)
{
	if (ScreenSonyNative()) {
		HRWinDrawBitmap(ScreenSonyRef(), bitmapP, x, Y);
		return;
	}
	WinDrawBitmap(bitmapP, x, Y);
}

void SEC_DRAW ScreenWinDrawChars(const Char *chars, Int16 len, Coord x, Coord y)
{
	if (ScreenSonyNative()) {
		HRWinDrawChars(ScreenSonyRef(), chars, len, x, y);
		return;
	}
	WinDrawChars(chars, len, x, y);
}

void SEC_DRAW ScreenWinDrawLine(Coord x1, Coord y1, Coord x2, Coord y2)
{
	if (ScreenSonyNative()) {
		HRWinDrawLine(ScreenSonyRef(), x1, y1, x2, y2);
		return;
	}
	WinDrawLine(x1, y1, x2, y2);
}

void SEC_DRAW ScreenWinDrawPixel(Coord x, Coord y)
{
	if (ScreenSonyNative()) {
		HRWinDrawPixel(ScreenSonyRef(), x, y);
		return;
	}
	WinDrawPixel(x, y);
}

void SEC_DRAW ScreenWinDrawRectangle(RectangleType *rP, UInt16 cornerDiam)
{
	if (ScreenSonyNative()) {
		HRWinDrawRectangle(ScreenSonyRef(), rP, cornerDiam);
		return;
	}
	WinDrawRectangle(rP, cornerDiam);
}

void SEC_DRAW ScreenWinEraseLine(Coord x1, Coord y1, Coord x2, Coord y2)
{
	if (ScreenSonyNative()) {
		HRWinEraseLine(ScreenSonyRef(), x1, y1, x2, y2);
		return;
	}
	WinEraseLine(x1, y1, x2, y2);
}

void SEC_DRAW ScreenWinEraseRectangle(RectangleType *rP, UInt16 cornerDiam)
{
	if (ScreenSonyNative()) {
		HRWinEraseRectangle(ScreenSonyRef(), rP, cornerDiam);
		return;
	}
	WinEraseRectangle(rP, cornerDiam);
}

void SEC_DRAW ScreenWinGetDisplayExtent(Coord *extentX, Coord *extentY)
{
	if (ScreenSonyNative()) {
		HRWinGetDisplayExtent(ScreenSonyRef(), extentX, extentY);
		return;
	}
	WinGetDisplayExtent(extentX, extentY);
}

IndexedColorType SEC_DRAW ScreenWinGetPixel(Coord x, Coord y)
{
	if (ScreenSonyNative()) {
		return HRWinGetPixel(ScreenSonyRef(), x, y);
	}
	return WinGetPixel(x, y);
}

void SEC_DRAW ScreenWinInvertRectangle(RectangleType *rP, UInt16 cornerDiam)
{
	if (ScreenSonyNative()) {
		HRWinInvertRectangle(ScreenSonyRef(), rP, cornerDiam);
		return;
	}
	WinInvertRectangle(rP, cornerDiam);
}

void SEC_DRAW ScreenWinScrollRectangle(RectangleType *rP, WinDirectionType direction, Coord distance, RectangleType *vacatedP)
{
	if (ScreenSonyNative()) {
		HRWinScrollRectangle(ScreenSonyRef(), rP, direction, distance, vacatedP);
		return;
	}
	WinScrollRectangle(rP, direction, distance, vacatedP);
}

// Sony clips require even origins and extents. Repaint and flush this area.
void SEC_DRAW ScreenAlignRect(RectangleType *r)
{
	Coord right, bottom;

	if (!ScreenSonyNative() || r->extent.x <= 0 || r->extent.y <= 0)
		return;
	right = (r->topLeft.x + r->extent.x + 1) & ~1;
	bottom = (r->topLeft.y + r->extent.y + 1) & ~1;
	r->topLeft.x &= ~1;
	r->topLeft.y &= ~1;
	r->extent.x = right - r->topLeft.x;
	r->extent.y = bottom - r->topLeft.y;
}

void SEC_DRAW ScreenWinSetClip(const RectangleType *rP)
{
	if (ScreenSonyNative()) {
		HRWinSetClip(ScreenSonyRef(), (RectangleType *)rP);
		return;
	}
	WinSetClip(rP);
}

FontID SEC_DRAW ScreenFont(FontID font)
{
	if (ScreenSonyNative())
		return (FontID)(HRFntSetFont(ScreenSonyRef(), font + 8) - 8);
	return FntSetFont(font);
}
