#!/usr/bin/env python3
"""Counts frame captures with the player's tank missing, from a
tools/tg50-flicker.replay run (tools/frond-tg50.py). The tank turns on the
spot at the view's centre on the TG50, where its black outline is the only
pure black; a frame with almost none there caught the tank erased.

On the TX the view is taller and the tank lower: --tx.

Usage: flicker.py [--tx] RUNDIR [RUNDIR...]
"""
import sys
from pathlib import Path
from PIL import Image

BOX = (148, 120, 174, 146)


def outline(path):
    im = Image.open(path).convert('RGB')
    return sum(1 for x in range(BOX[0], BOX[2]) for y in range(BOX[1], BOX[3])
               if im.getpixel((x, y)) == (0, 0, 0))


args = sys.argv[1:]
if args and args[0] == '--tx':
    BOX = (148, 184, 174, 210)
    args = args[1:]
for run in args:
    frames = sorted(Path(run).glob('replay-snapshot-*.png'))[1:]
    missing = [f.name for f in frames if outline(f) < 10]
    print(f'{run}: {len(missing)} of {len(frames)} frames without the tank')
    for name in missing[:10]:
        print('  ', name)
