#!/usr/bin/env python3
# Writes src/game/tables.h: the quarter-wave cosine table and the CRC-32
# table used by the game rules. Run from the project root.
#
# The cosine table follows WinBolo's utilCalcDistanceHP: an angle in
# bradians becomes radians as (360 / 256 * a) / 360 * 6.28318 (WinBolo's
# own 2 pi), and the entry is the shell step of 32 world units scaled by
# 256 and rounded half away from zero, so kCos[j] = round(8192 * cos) for
# j = 0..2048 in 1/32 bradian steps (a quarter turn).
import math

def round_away(v):
    return int(v + 0.5) if v >= 0 else int(v - 0.5)

cos = []
for j in range(2049):
    a = j / 32.0
    rad = (360.0 / 256.0 * a) / 360.0 * (2.0 * 3.14159)
    cos.append(round_away(32 * math.cos(rad) * 256.0))

crc = []
for n in range(256):
    c = n
    for _ in range(8):
        c = (c >> 1) ^ (0xEDB88320 if c & 1 else 0)
    crc.append(c)

out = ["// Made by tools/gentables.py. Do not edit.",
       "// Included once, by util.c, so the tables stay static to that file.",
       "",
       "static const uint16_t kCos[2049] = {"]
for i in range(0, len(cos), 12):
    out.append("\t" + ", ".join(str(v) for v in cos[i:i + 12]) + ",")
out.append("};")
out.append("")
out.append("static const uint32_t kCrc[256] = {")
for i in range(0, 256, 6):
    out.append("\t" + ", ".join("0x%08lXUL" % v for v in crc[i:i + 6]) + ",")
out.append("};")
open("src/game/tables.h", "w").write("\n".join(out) + "\n")
