// Integer helpers for the game rules: angles, distances, the CRC.
//
// The angle and distance rules follow WinBolo 2 (src/bolo/util.c),
// Copyright (c) 1998-2026 John Morrison, GPLv3. This version Copyright (c)
// 2026 the PalmBolo authors, GPLv3.

#ifndef UTIL_H
#define UTIL_H

#include <stdint.h>

#define ANGLE_ONE       32      // 1/32 bradian units in one bradian
#define ANGLE_MAX       8192    // a full turn

// 16 x 16 to 32 bit multiply, one muls.w on the 68000.
#define MUL16(a, b)     ((int32_t)(int16_t)(a) * (int16_t)(b))

// WinBolo's utilCalcDistance: speed times the cosine and sine of angle
// (north is 0, clockwise), each rounded half away from zero. speed is
// 0..1023. WinBolo works this out in doubles; here it comes from the
// quarter-wave table (see tools/gentables.py).
void util_distance(int16_t *dx, int16_t *dy, uint16_t angle, uint16_t speed);

// WinBolo's utilCalcDistanceHP for the shell speed: the step a tick in
// 1/256 world units.
void util_shell_step(int16_t *dx, int16_t *dy, uint16_t angle);

// utilGetDir: 0..15, rounding the way WinBolo does (a heading exactly
// between two sectors goes to the lower one).
uint8_t util_dir16(uint16_t angle);

// utilCalcAngle: the heading from point 1 to point 2 in 1/32 bradians,
// 0..8192. WinBolo works it out with atan in doubles and keeps the
// fraction; here it is rounded to 1/32 bradian. 0 when the points match.
uint16_t util_angle(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2);

// Integer square root, rounded down.
uint16_t util_isqrt(uint32_t v);

// CRC-32 (the zip polynomial) of n bytes, continuing from crc. Start with
// 0xFFFFFFFF and invert at the end.
uint32_t util_crc(uint32_t crc, const uint8_t *p, uint16_t n);

// Checksum of the const tables, for the self-test that they read back
// right on the device.
uint32_t util_table_check(void);
#define UTIL_TABLE_CHECK 0x3191B593UL

#endif
