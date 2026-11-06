// Drawing straight into a window's pixels on OS 3.0 to 3.3, where each
// WinCopyRectangle or WinDrawLine costs about a millisecond on a 16 MHz
// Palm. Only 1 and 2 bit windows at normal density. A window's pixels are
// found through its WindowType fields (the OS 3.x layout, still there in
// 3.5) and checked by drawing a probe with the OS and reading it back, so a
// window that doesn't look as expected is left to the OS calls.
#define ALLOW_ACCESS_TO_INTERNALS_OF_WINDOWS
#include "palmbolo.h"

// Finds w's pixels and checks them with a probe that erases row 0 and 1
// from x 0 to 15 and puts a pixel at (1, 1), then puts back what was there
// if the probe passed. Fills in b and returns true if they can be written
// directly.
Boolean SEC_DRAW BlitFind(WinHandle w, BlitWin *b)
{
	WindowType *wp = (WindowType *)w;
	WinHandle old;
	UInt16 depth = ScreenDepth(), xoff;
	UInt8 *p, keep[8];
	UInt16 i, n;
	Boolean ok;

	b->bits = NULL;
	if (!w || ScreenNewWin() || ScreenHiRes() || (depth != 1 && depth != 2))
		return false;
	if (!wp->displayAddrV20 || wp->displayWidthV20 < 16 || wp->displayHeightV20 < 2)
		return false;
	// The window's top left must start a byte, and the window lie inside
	// the display.
	xoff = (UInt16)wp->windowBounds.topLeft.x * depth;
	if (wp->windowBounds.topLeft.x < 0 || wp->windowBounds.topLeft.y < 0 || (xoff & 7) ||
	    wp->windowBounds.topLeft.x + wp->windowBounds.extent.x > wp->displayWidthV20 ||
	    wp->windowBounds.topLeft.y + wp->windowBounds.extent.y > wp->displayHeightV20 ||
	    wp->windowBounds.extent.x < 16 || wp->windowBounds.extent.y < 2)
		return false;
	b->depth = depth;
	b->rowBytes = (UInt16)((((UInt16)wp->displayWidthV20 * depth + 15) >> 4) << 1);
	b->w = wp->windowBounds.extent.x;
	b->h = wp->windowBounds.extent.y;
	p = (UInt8 *)wp->displayAddrV20 + (UInt32)wp->windowBounds.topLeft.y * b->rowBytes + (xoff >> 3);
	b->bits = p;
	b->win = w;

	// x 0 to 15 is 2 bytes a row at 1 bit, 4 at 2 bits.
	n = depth * 2;
	for (i = 0; i < n; i++) {
		keep[i] = p[i];
		keep[n + i] = p[b->rowBytes + i];
	}
	old = WinSetDrawWindow(w);
	WinEraseLine(0, 0, 15, 0);
	WinEraseLine(0, 1, 15, 1);
	ok = p[0] == 0 && p[1] == 0 && p[b->rowBytes] == 0 && p[b->rowBytes + 1] == 0;
	WinDrawLine(1, 1, 1, 1);
	// Pixel 1 is bit 6 at 1 bit, bits 5 and 4 at 2 bits; black is all ones.
	ok = ok && p[0] == 0 && p[b->rowBytes] == (depth == 1 ? 0x40 : 0x30) &&
		p[b->rowBytes + 1] == 0;
	WinEraseLine(1, 1, 1, 1);
	WinSetDrawWindow(old);
	if (ok)
		for (i = 0; i < n; i++) {
			p[i] = keep[i];
			p[b->rowBytes + i] = keep[n + i];
		}
	else
		b->bits = NULL;
	LOG(("blit: window %d x %d depth %u row bytes %u: %s", (Int16)b->w, (Int16)b->h, depth,
		b->rowBytes, ok ? "direct" : "probe failed, OS calls"));
	return ok;
}

// y * rowBytes as one 16 by 16 bit multiply; a 32-bit one is a library
// call on the 68000.
static __inline__ UInt32 Row(Int16 y, UInt16 rowBytes)
{
	return (UInt32)(UInt16)y * rowBytes;
}

// Copies the gTs by gTs cell at pixel (sx, sy) of src to (x, y) in dst,
// clipped to clip (in dst pixels). 8 by 8 cells only, with sx a multiple
// of 8.
void SEC_DRAW BlitCell(const BlitWin *src, Coord sx, Coord sy, const BlitWin *dst,
	Coord x, Coord y, const RectangleType *clip)
{
	Coord c0 = 0, c1 = 8, r0 = 0, r1 = 8, r;
	Int16 d = (Int16)dst->depth, xb = x * d;     // signed: x may be left of the window
	UInt16 shift, mhi, mlo;
	UInt32 m;
	const UInt8 *s;
	UInt16 *o;
	UInt8 *db = dst->bits;

	if (clip->topLeft.x > x)
		c0 = clip->topLeft.x - x;
	if (clip->topLeft.x + clip->extent.x < x + 8)
		c1 = clip->topLeft.x + clip->extent.x - x;
	if (clip->topLeft.y > y)
		r0 = clip->topLeft.y - y;
	if (clip->topLeft.y + clip->extent.y < y + 8)
		r1 = clip->topLeft.y + clip->extent.y - y;
	if (c0 >= c1 || r0 >= r1)
		return;
	// The words below need an even address: an odd dst starts a byte
	// early, 8 bits further in.
	if ((UInt32)db & 1) {
		db--;
		xb += 8;
	}
	s = src->bits + Row(sy + r0, src->rowBytes) + ((UInt16)(sx * d) >> 3);
	// Whole rows at 2 bits that start a byte, the view's usual case: two
	// bytes a row.
	if (d == 2 && c0 == 0 && c1 == 8 && !(xb & 7)) {
		UInt8 *b = db + Row(y + r0, dst->rowBytes) + (xb >> 3);
		UInt16 srb = src->rowBytes, drb = dst->rowBytes;

		for (r = r0; r < r1; r++) {
			b[0] = s[0];
			b[1] = s[1];
			s += srb;
			b += drb;
		}
		return;
	}
	// At 2 bits, columns clipped at the cell's middle (the strip a 4 pixel
	// sideways scroll uncovers): one whole byte a row.
	if (d == 2 && !(xb & 7) && !(c0 & 3) && !(c1 & 3)) {
		UInt8 *b = db + Row(y + r0, dst->rowBytes) + (xb >> 3);
		UInt16 srb = src->rowBytes, drb = dst->rowBytes;

		if (c0) {
			s++;
			b++;
		}
		for (r = r0; r < r1; r++) {
			b[0] = s[0];
			s += srb;
			b += drb;
		}
		return;
	}
	// The cell's columns c0 to c1 - 1 as bits of a 16-bit row, left aligned
	// (a 1-bit row is the top byte).
	m = (0xFFFFUL >> (c0 * d)) & ~(0xFFFFUL >> (c1 * d)) & 0xFFFFUL;
	shift = (UInt16)(xb & 15);
	m <<= 16 - shift;
	mhi = (UInt16)(m >> 16);
	mlo = (UInt16)m;
	o = (UInt16 *)(db + Row(y + r0, dst->rowBytes) + (Int32)(xb >> 4) * 2);
	for (r = r0; r < r1; r++) {
		UInt32 v = (d == 2 ? ((UInt32)s[0] << 8 | s[1]) : (UInt32)s[0] << 8) << (16 - shift);

		if (mhi)
			o[0] = (UInt16)((o[0] & ~mhi) | ((UInt16)(v >> 16) & mhi));
		if (mlo)
			o[1] = (UInt16)((o[1] & ~mlo) | ((UInt16)v & mlo));
		s += src->rowBytes;
		o = (UInt16 *)((UInt8 *)o + dst->rowBytes);
	}
}

// Draws sprite cell (sx, sy) of spr at (x, y) in dst through mask cell
// (mx, my) of mask, as winMask then winOverlay do: pixels set in the mask
// are cleared, then the sprite's pixels are or-ed in. Clipped to clip.
void SEC_DRAW BlitSprite(const BlitWin *mask, Coord mx, Coord my, const BlitWin *spr,
	Coord sx, Coord sy, const BlitWin *dst, Coord x, Coord y, const RectangleType *clip)
{
	Coord c0 = 0, c1 = 8, r0 = 0, r1 = 8, r;
	Int16 d = (Int16)dst->depth, xb = x * d;     // signed: x may be left of the window
	UInt16 shift, chi, clo;
	UInt32 m;
	const UInt8 *s, *k;
	UInt16 *o;
	UInt8 *db = dst->bits;

	if (clip->topLeft.x > x)
		c0 = clip->topLeft.x - x;
	if (clip->topLeft.x + clip->extent.x < x + 8)
		c1 = clip->topLeft.x + clip->extent.x - x;
	if (clip->topLeft.y > y)
		r0 = clip->topLeft.y - y;
	if (clip->topLeft.y + clip->extent.y < y + 8)
		r1 = clip->topLeft.y + clip->extent.y - y;
	if (c0 >= c1 || r0 >= r1)
		return;
	// The words below need an even address: an odd dst starts a byte
	// early, 8 bits further in.
	if ((UInt32)db & 1) {
		db--;
		xb += 8;
	}
	shift = (UInt16)(xb & 15);
	s = spr->bits + Row(sy + r0, spr->rowBytes) + ((UInt16)(sx * d) >> 3);
	k = mask->bits + Row(my + r0, mask->rowBytes) + ((UInt16)(mx * d) >> 3);
	o = (UInt16 *)(db + Row(y + r0, dst->rowBytes) + (Int32)(xb >> 4) * 2);
	// Whole rows at 2 bits from word aligned cells, the usual case: a row
	// is one word of sprite and mask, shifted across one or two words.
	// Unclipped, the shifted bits all fall inside the columns, so no
	// column mask is needed.
	if (d == 2 && c0 == 0 && c1 == 8 && !(((UInt32)s | (UInt32)k) & 1)) {
		const UInt16 *sw = (const UInt16 *)s, *kw = (const UInt16 *)k;
		UInt16 srb = spr->rowBytes, krb = mask->rowBytes, drb = dst->rowBytes, up;

		if (shift == 0) {
			for (r = r0; r < r1; r++) {
				o[0] = (UInt16)((o[0] & ~*kw) | *sw);
				sw = (const UInt16 *)((const UInt8 *)sw + srb);
				kw = (const UInt16 *)((const UInt8 *)kw + krb);
				o = (UInt16 *)((UInt8 *)o + drb);
			}
			return;
		}
		up = (UInt16)(16 - shift);
		for (r = r0; r < r1; r++) {
			UInt32 v = (UInt32)*sw << up, w = (UInt32)*kw << up;

			o[0] = (UInt16)((o[0] & ~(UInt16)(w >> 16)) | (UInt16)(v >> 16));
			o[1] = (UInt16)((o[1] & ~(UInt16)w) | (UInt16)v);
			sw = (const UInt16 *)((const UInt8 *)sw + srb);
			kw = (const UInt16 *)((const UInt8 *)kw + krb);
			o = (UInt16 *)((UInt8 *)o + drb);
		}
		return;
	}
	m = (0xFFFFUL >> (c0 * d)) & ~(0xFFFFUL >> (c1 * d)) & 0xFFFFUL;
	m <<= 16 - shift;
	chi = (UInt16)(m >> 16);
	clo = (UInt16)m;
	for (r = r0; r < r1; r++) {
		UInt32 v, w;
		UInt16 hi, lo;

		if (d == 2) {
			v = (UInt32)s[0] << 8 | s[1];
			w = (UInt32)k[0] << 8 | k[1];
		} else {
			v = (UInt32)s[0] << 8;
			w = (UInt32)k[0] << 8;
		}
		v <<= 16 - shift;
		w <<= 16 - shift;
		hi = (UInt16)(w >> 16) & chi;
		lo = (UInt16)w & clo;
		if (chi)
			o[0] = (UInt16)((o[0] & ~hi) | ((UInt16)(v >> 16) & chi));
		if (clo)
			o[1] = (UInt16)((o[1] & ~lo) | ((UInt16)v & clo));
		s += spr->rowBytes;
		k += mask->rowBytes;
		o = (UInt16 *)((UInt8 *)o + dst->rowBytes);
	}
}

// Moves n bytes from s to d, which may overlap, like MemMove. When both
// have the same parity it goes by 32 bytes of longs at a time, else by 16
// single bytes, both unrolled with post-increment or pre-decrement moves,
// as the 68000 can't read a word at an odd address.
#ifdef __m68k__
#define MOVE8(op, d, s, k) __asm__ __volatile__("1:\n" \
	op "\n" op "\n" op "\n" op "\n" op "\n" op "\n" op "\n" op "\n" \
	"subq.w #1,%2\n\tbne 1b" : "=a" (d), "=a" (s), "=d" (k) : "0" (d), "1" (s), "2" (k) : "memory")
#define UP_LONGS(d, s, k)   MOVE8("move.l (%1)+,(%0)+", d, s, k)
#define DOWN_LONGS(d, s, k) MOVE8("move.l -(%1),-(%0)", d, s, k)
#define UP_BYTES(d, s, k)   MOVE8("move.b (%1)+,(%0)+\n\tmove.b (%1)+,(%0)+", d, s, k)
#define DOWN_BYTES(d, s, k) MOVE8("move.b -(%1),-(%0)\n\tmove.b -(%1),-(%0)", d, s, k)
#else
#define UP_LONGS(d, s, k)   do { MemMove(d, s, (UInt32)k * 32); d += k * 32; s += k * 32; } while (0)
#define DOWN_LONGS(d, s, k) do { d -= k * 32; s -= k * 32; MemMove(d, s, (UInt32)k * 32); } while (0)
#define UP_BYTES(d, s, k)   do { MemMove(d, s, (UInt32)k * 16); d += k * 16; s += k * 16; } while (0)
#define DOWN_BYTES(d, s, k) do { d -= k * 16; s -= k * 16; MemMove(d, s, (UInt32)k * 16); } while (0)
#endif

void SEC_DRAW BlitMove(UInt8 *d, const UInt8 *s, UInt32 n)
{
	UInt16 k;

	if (d == s || n == 0)
		return;
	if (d < s) {
		if (!(((UInt32)d ^ (UInt32)s) & 1)) {
			if ((UInt32)d & 1) {
				*d++ = *s++;
				n--;
			}
			if ((k = (UInt16)(n >> 5)) != 0)
				UP_LONGS(d, s, k);
			n &= 31;
		} else {
			if ((k = (UInt16)(n >> 4)) != 0)
				UP_BYTES(d, s, k);
			n &= 15;
		}
		while (n--)
			*d++ = *s++;
		return;
	}
	// Overlapping the other way: from the end down.
	d += n;
	s += n;
	if (!(((UInt32)d ^ (UInt32)s) & 1)) {
		if ((UInt32)d & 1) {
			*--d = *--s;
			n--;
		}
		if ((k = (UInt16)(n >> 5)) != 0)
			DOWN_LONGS(d, s, k);
		n &= 31;
	} else {
		if ((k = (UInt16)(n >> 4)) != 0)
			DOWN_BYTES(d, s, k);
		n &= 15;
	}
	while (n--)
		*--d = *--s;
}

// Copies rows of n bytes from s to d, which don't overlap, stepping srb and
// drb bytes a row: by longs when s and d have the same parity, else by
// bytes.
void SEC_DRAW BlitRows(UInt8 *d, UInt16 drb, const UInt8 *s, UInt16 srb, UInt16 n, UInt16 rows)
{
	for (; rows; rows--, d += drb, s += srb) {
		UInt8 *dd = d;
		const UInt8 *ss = s;
		UInt16 k, m = n;

		if (!(((UInt32)dd ^ (UInt32)ss) & 1)) {
			if (((UInt32)dd & 1) && m) {
				*dd++ = *ss++;
				m--;
			}
			if ((k = (UInt16)(m >> 5)) != 0)
				UP_LONGS(dd, ss, k);
			m &= 31;
			for (; m >= 4; m -= 4, dd += 4, ss += 4)
				*(UInt32 *)dd = *(const UInt32 *)ss;
		} else {
			if ((k = (UInt16)(m >> 4)) != 0)
				UP_BYTES(dd, ss, k);
			m &= 15;
		}
		while (m--)
			*dd++ = *ss++;
	}
}
