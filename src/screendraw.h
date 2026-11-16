// Drawing calls in tileview, panel and compose use native pixels.
#ifndef SCREENDRAW_H
#define SCREENDRAW_H
void SEC_DRAW ScreenAlignRect(RectangleType *r);
void SEC_DRAW ScreenWinCopyRectangle(WinHandle srcWin, WinHandle dstWin, RectangleType *srcRect, Coord destX, Coord destY, WinDrawOperation mode);
#define WinCopyRectangle ScreenWinCopyRectangle
WinHandle SEC_DRAW ScreenWinCreateBitmapWindow(BitmapType *bitmapP, UInt16 *error);
#define WinCreateBitmapWindow ScreenWinCreateBitmapWindow
WinHandle SEC_DRAW ScreenWinCreateOffscreenWindow(Coord width, Coord height, WindowFormatType format, UInt16 *error);
#define WinCreateOffscreenWindow ScreenWinCreateOffscreenWindow
void SEC_DRAW ScreenWinDrawBitmap(BitmapPtr bitmapP, Coord x, Coord Y);
#define WinDrawBitmap ScreenWinDrawBitmap
void SEC_DRAW ScreenWinDrawChars(const Char *chars, Int16 len, Coord x, Coord y);
#define WinDrawChars ScreenWinDrawChars
void SEC_DRAW ScreenWinDrawLine(Coord x1, Coord y1, Coord x2, Coord y2);
#define WinDrawLine ScreenWinDrawLine
void SEC_DRAW ScreenWinDrawPixel(Coord x, Coord y);
#define WinDrawPixel ScreenWinDrawPixel
void SEC_DRAW ScreenWinDrawRectangle(RectangleType *rP, UInt16 cornerDiam);
#define WinDrawRectangle ScreenWinDrawRectangle
void SEC_DRAW ScreenWinEraseLine(Coord x1, Coord y1, Coord x2, Coord y2);
#define WinEraseLine ScreenWinEraseLine
void SEC_DRAW ScreenWinEraseRectangle(RectangleType *rP, UInt16 cornerDiam);
#define WinEraseRectangle ScreenWinEraseRectangle
void SEC_DRAW ScreenWinGetDisplayExtent(Coord *extentX, Coord *extentY);
#define WinGetDisplayExtent ScreenWinGetDisplayExtent
IndexedColorType SEC_DRAW ScreenWinGetPixel(Coord x, Coord y);
#define WinGetPixel ScreenWinGetPixel
void SEC_DRAW ScreenWinInvertRectangle(RectangleType *rP, UInt16 cornerDiam);
#define WinInvertRectangle ScreenWinInvertRectangle
void SEC_DRAW ScreenWinScrollRectangle(RectangleType *rP, WinDirectionType direction, Coord distance, RectangleType *vacatedP);
#define WinScrollRectangle ScreenWinScrollRectangle
void SEC_DRAW ScreenWinSetClip(const RectangleType *rP);
#define WinSetClip ScreenWinSetClip
FontID SEC_DRAW ScreenFont(FontID font);
#define FntSetFont ScreenFont
#endif
