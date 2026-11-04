// Integer helpers for the game rules. WinBolo 2 (src/bolo/util.c),
// Copyright (c) 1998-2026 John Morrison, GPLv3. This version Copyright (c)
// 2026 the PalmBolo authors, GPLv3.
//
// The tables are static to this file, as const tables must be in the
// prc-tools build (see tools/segcheck.py).
#include "util.h"
#include "tables.h"

// WinBolo turns an angle into radians after taking 64 bradians off it, so
// angle 0 (north) points along -y. j is that angle in 1/32 bradians,
// 0..8191, and the table gives cos and sin for it at scale 8192 using the
// quarter-wave symmetry. Float replacement: WinBolo calls cos() and sin()
// in doubles; the first quadrant here is exact, the others mirror it.
static void trig(uint16_t angle, int16_t *c, int16_t *s)
{
	uint16_t j = (uint16_t)((angle + ANGLE_MAX - 64 * ANGLE_ONE) & (ANGLE_MAX - 1));

	if (j <= 2048) {
		*c = (int16_t)kCos[j];
		*s = (int16_t)kCos[2048 - j];
	} else if (j <= 4096) {
		*c = (int16_t)-(int16_t)kCos[4096 - j];
		*s = (int16_t)kCos[j - 2048];
	} else if (j <= 6144) {
		*c = (int16_t)-(int16_t)kCos[j - 4096];
		*s = (int16_t)-(int16_t)kCos[6144 - j];
	} else {
		*c = (int16_t)kCos[8192 - j];
		*s = (int16_t)-(int16_t)kCos[j - 6144];
	}
}

// round(speed * t / 8192), half away from zero.
static int16_t scale(int16_t t, uint16_t speed)
{
	uint32_t m;

	if (t >= 0) {
		m = (uint32_t)(uint16_t)t * speed;
		return (int16_t)((m + 4096) >> 13);
	}
	m = (uint32_t)(uint16_t)(-t) * speed;
	return (int16_t)-(int16_t)((m + 4096) >> 13);
}

void util_distance(int16_t *dx, int16_t *dy, uint16_t angle, uint16_t speed)
{
	int16_t c, s;

	trig(angle, &c, &s);
	*dx = scale(c, speed);
	*dy = scale(s, speed);
}

void util_shell_step(int16_t *dx, int16_t *dy, uint16_t angle)
{
	trig(angle, dx, dy);
}

uint8_t util_dir16(uint16_t angle)
{
	uint16_t d;

	if (angle <= 8 * ANGLE_ONE)
		return 0;
	d = (uint16_t)(((angle - (8 * ANGLE_ONE + 1)) >> 9) + 1);
	return d >= 16 ? 0 : (uint8_t)d;
}

// atan of min / max in 1/32 bradians, from the table with linear steps
// between entries. max is not 0.
static uint16_t atan_ratio(uint16_t mn, uint16_t mx)
{
	uint16_t r = (uint16_t)(((uint32_t)mn << 14) / mx);   // 0..16384
	uint16_t i = (uint16_t)(r >> 6), f = (uint16_t)(r & 63);
	uint16_t v = kAtan[i];

	if (f)
		v = (uint16_t)(v + (((uint16_t)(kAtan[i + 1] - v) * f) >> 6));
	return (uint16_t)((v + 8) >> 4);
}

uint16_t util_angle(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2)
{
	uint16_t gx = x2 >= x1 ? (uint16_t)(x2 - x1) : (uint16_t)(x1 - x2);
	uint16_t gy = y2 >= y1 ? (uint16_t)(y2 - y1) : (uint16_t)(y1 - y2);
	uint16_t a;
	uint8_t xle = x2 <= x1, xgt = x2 > x1, yle = y2 <= y1, yge = y2 >= y1;

	if (gx == 0 && gy == 0)
		return 0;
	// a = atan(gx / gy)
	if (gx <= gy)
		a = atan_ratio(gx, gy);
	else
		a = (uint16_t)(64 * ANGLE_ONE - atan_ratio(gy, gx));
	if (xle && yle)
		a = (uint16_t)(ANGLE_MAX - a);
	else if (xgt && yge)
		a = (uint16_t)(128 * ANGLE_ONE - a);
	else if (xle && yge)
		a = (uint16_t)(a + 128 * ANGLE_ONE);
	return a;
}

uint16_t util_isqrt(uint32_t v)
{
	uint32_t r = 0, bit = 1UL << 30;

	while (bit > v)
		bit >>= 2;
	while (bit) {
		if (v >= r + bit) {
			v -= r + bit;
			r = (r >> 1) + bit;
		} else {
			r >>= 1;
		}
		bit >>= 2;
	}
	return (uint16_t)r;
}

uint32_t util_crc(uint32_t crc, const uint8_t *p, uint16_t n)
{
	while (n--)
		crc = kCrc[(crc ^ *p++) & 0xFF] ^ (crc >> 8);
	return crc;
}

uint32_t util_table_check(void)
{
	uint32_t crc = 0xFFFFFFFFUL;
	uint16_t i;
	uint8_t b[2];

	for (i = 0; i < 2049; i++) {
		b[0] = (uint8_t)(kCos[i] >> 8);
		b[1] = (uint8_t)kCos[i];
		crc = util_crc(crc, b, 2);
	}
	for (i = 0; i < 257; i++) {
		b[0] = (uint8_t)(kAtan[i] >> 8);
		b[1] = (uint8_t)kAtan[i];
		crc = util_crc(crc, b, 2);
	}
	return ~crc;
}
