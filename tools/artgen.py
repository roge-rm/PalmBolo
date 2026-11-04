#!/usr/bin/env python3
# Draws the starting art into res/art: terrain8.png, terrain16.png,
# sprites8.png, sprites16.png and layout.txt, using the materials in
# res/art/palette.txt. Running it again overwrites hand edits to the PNGs,
# so after the first run the PNGs are the source and this is only a way to
# remake them from scratch.
#
# Each sheet is a grid 16 cells wide of square cells (8 or 16 pixels).
# layout.txt names the cells in order; tools/art2palm.py and the C code
# (through build/art/art.h) go by those names.
#
# Terrain with neighbour variants (road, building, river, deep sea, forest,
# boat) has one cell per variant WinBolo 2 picks in src/bolo/screencalc.c.
# Each variant is drawn from a typical neighbourhood that gives it: this
# file has its own copy of WinBolo's choice logic, runs every neighbourhood
# through it and draws the most common look among those that give the
# variant.
#
# Usage: artgen.py [OUTDIR]   (default res/art)
#        artgen.py --icons [OUTDIR]   only redraws the icon cells in the
#        sprite sheets that are there
import math
import os
import sys
from collections import Counter

from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
ART = os.path.join(HERE, "..", "res", "art")


def load_palette(path):
    mats = {}
    for line in open(path):
        line = line.split("#")[0].split()
        if not line:
            continue
        idx, name = int(line[0]), line[1]
        r, g, b = int(line[2]), int(line[3]), int(line[4])
        mats[name] = (idx, (r, g, b), int(line[5]), int(line[6]), line[7])
    return mats


# ---------------------------------------------------------------------------
# Terrain classes and WinBolo's variant choice (screencalc.c), on the eight
# neighbours in WinBolo's order: aboveLeft, above, aboveRight, left, right,
# belowLeft, below, belowRight.

BUILDING, RIVER, SWAMP, CRATER, ROAD, FOREST, RUBBLE, GRASS, HALF, BOAT = range(10)
DEEP = 255
AL, A, AR, L, R, BL, B, BR = range(8)


def calc_road(n):
    n = [RIVER if t in (DEEP, BOAT) else t for t in n]
    al, a, ar, l, r, bl, b, br = n
    if al != ROAD and a == ROAD and ar != ROAD and l == ROAD and r == ROAD and bl != ROAD and b == ROAD and br != ROAD:
        return "ROAD_CROSS"
    if a == ROAD and l == ROAD and r == ROAD and b == ROAD:
        return "ROAD_SOLID"
    if l == RIVER and r == RIVER and a == RIVER and b == RIVER:
        return "ROAD_WATER11"
    if r == ROAD and b == ROAD and l == RIVER and a == RIVER:
        return "ROAD_WATER5"
    if l == ROAD and b == ROAD and r == RIVER and a == RIVER:
        return "ROAD_WATER6"
    if a == ROAD and l == ROAD and b == RIVER and r == RIVER:
        return "ROAD_WATER7"
    if r == ROAD and a == ROAD and l == RIVER and b == RIVER:
        return "ROAD_WATER8"
    if a == RIVER and b == RIVER:
        return "ROAD_WATER9"
    if l == RIVER and r == RIVER:
        return "ROAD_WATER10"
    if a == RIVER and b == ROAD:
        return "ROAD_WATER1"
    if r == RIVER and l == ROAD:
        return "ROAD_WATER2"
    if b == RIVER and a == ROAD:
        return "ROAD_WATER3"
    if l == RIVER and r == ROAD:
        return "ROAD_WATER4"
    if r == ROAD and b == ROAD and a == ROAD and (ar == ROAD or br == ROAD):
        return "ROAD_SIDE1"
    if l == ROAD and r == ROAD and b == ROAD and (bl == ROAD or br == ROAD):
        return "ROAD_SIDE2"
    if l == ROAD and a == ROAD and b == ROAD and (bl == ROAD or al == ROAD):
        return "ROAD_SIDE3"
    if l == ROAD and r == ROAD and a == ROAD and (ar == ROAD or al == ROAD):
        return "ROAD_SIDE4"
    if l == ROAD and r == ROAD and b == ROAD:
        return "ROAD_T1"
    if l == ROAD and a == ROAD and b == ROAD:
        return "ROAD_T2"
    if l == ROAD and r == ROAD and a == ROAD:
        return "ROAD_T3"
    if r == ROAD and a == ROAD and b == ROAD:
        return "ROAD_T4"
    if b == ROAD and r == ROAD and br == ROAD:
        return "ROAD_CORNER5"
    if b == ROAD and l == ROAD and bl == ROAD:
        return "ROAD_CORNER6"
    if a == ROAD and l == ROAD and al == ROAD:
        return "ROAD_CORNER7"
    if a == ROAD and r == ROAD and ar == ROAD:
        return "ROAD_CORNER8"
    if b == ROAD and r == ROAD:
        return "ROAD_CORNER1"
    if b == ROAD and l == ROAD:
        return "ROAD_CORNER2"
    if a == ROAD and l == ROAD:
        return "ROAD_CORNER3"
    if a == ROAD and r == ROAD:
        return "ROAD_CORNER4"
    if r == ROAD or l == ROAD:
        return "ROAD_HORZ"
    if a == ROAD or b == ROAD:
        return "ROAD_VERT"
    return "ROAD_SOLID"


def calc_boat(n):
    n = [RIVER if t in (DEEP, BOAT) else t for t in n]
    al, a, ar, l, r, bl, b, br = n
    if a != RIVER and l != RIVER:
        return "BOAT_5"
    if a != RIVER and r != RIVER:
        return "BOAT_6"
    if b != RIVER and r != RIVER:
        return "BOAT_7"
    if b != RIVER and l != RIVER:
        return "BOAT_4"
    if l != RIVER:
        return "BOAT_2"
    if r != RIVER:
        return "BOAT_3"
    if b != RIVER:
        return "BOAT_0"
    return "BOAT_1"


def calc_building(n):
    n = [BUILDING if t == HALF else t for t in n]
    al, a, ar, l, r, bl, b, br = [t == BUILDING for t in n]
    if al and a and ar and l and r and bl and b and br:
        return "BUILD_SOLID"
    if r and a and b and l and not ar and not al and not br and not bl:
        return "BUILD_CROSS"
    if r and a and b and l and not ar and not al and not br and bl:
        return "BUILD_MOST1"
    if r and a and b and l and not ar and al and not br and not bl:
        return "BUILD_MOST2"
    if r and a and b and l and not ar and not al and br and not bl:
        return "BUILD_MOST3"
    if r and a and b and l and ar and not al and not br and not bl:
        return "BUILD_MOST4"
    if al and a and l and r and bl and b and br:
        return "BUILD_SIDECORN13"
    if a and ar and l and r and bl and b and br:
        return "BUILD_SIDECORN14"
    if al and a and ar and l and r and bl and b:
        return "BUILD_SIDECORN15"
    if al and a and ar and l and r and b and br:
        return "BUILD_SIDECORN16"
    if l and r and a and b and ar and bl and not al and not br:
        return "BUILD_TWIST1"
    if l and r and a and b and br and al and not ar and not bl:
        return "BUILD_TWIST2"
    if a and l and r and b and br and ar:
        return "BUILD_SIDECORN5"
    if a and l and r and b and bl and al:
        return "BUILD_SIDECORN6"
    if a and l and r and b and bl and br:
        return "BUILD_SIDECORN7"
    if a and l and r and b and al and ar:
        return "BUILD_SIDECORN8"
    if a and b and l and not r and bl and not al:
        return "BUILD_SIDECORN9"
    if a and b and r and br and not l and not ar:
        return "BUILD_SIDECORN10"
    if a and b and r and ar and not br:
        return "BUILD_SIDECORN11"
    if a and b and l and al and not bl:
        return "BUILD_SIDECORN12"
    if r and a and l and not b and not al and not ar:
        return "BUILD_T1"
    if r and b and l and not bl and not br:
        return "BUILD_T2"
    if r and a and b and not ar and not br:
        return "BUILD_T3"
    if b and a and l and not al and not bl:
        return "BUILD_T4"
    if l and r and a and ar and not al:
        return "BUILD_SIDECORN1"
    if l and r and a and al and not ar:
        return "BUILD_SIDECORN2"
    if l and r and b and bl and not br:
        return "BUILD_SIDECORN3"
    if l and r and b and not a and br and not bl:
        return "BUILD_SIDECORN4"
    if r and a and b:
        return "BUILD_SIDE1"
    if l and a and b:
        return "BUILD_SIDE2"
    if r and l and b:
        return "BUILD_SIDE3"
    if r and a and l:
        return "BUILD_SIDE4"
    if r and b and br:
        return "BUILD_CORNER1"
    if l and b and bl:
        return "BUILD_CORNER2"
    if r and a and ar:
        return "BUILD_CORNER3"
    if l and a and al:
        return "BUILD_CORNER4"
    if r and b:
        return "BUILD_L1"
    if l and b:
        return "BUILD_L2"
    if r and a:
        return "BUILD_L3"
    if l and a:
        return "BUILD_L4"
    if l and r:
        return "BUILD_HORZ"
    if a and b:
        return "BUILD_VERT"
    if r:
        return "BUILD_HORZEND1"
    if l:
        return "BUILD_HORZEND2"
    if b:
        return "BUILD_VERTEND1"
    if a:
        return "BUILD_VERTEND2"
    return "BUILD_SINGLE"


def calc_river(n):
    n = [RIVER if t in (DEEP, BOAT) else t for t in n]
    al, a, ar, l, r, bl, b, br = [t in (RIVER, ROAD) for t in n]
    if not a and not b and not r and not l:
        return "RIVER_SURROUND"
    if not a and not b and r and not l:
        return "RIVER_END1"
    if not a and not b and not r and l:
        return "RIVER_END2"
    if not a and b and not r and not l:
        return "RIVER_END3"
    if a and not b and not r and not l:
        return "RIVER_END4"
    if not a and not l:
        return "RIVER_CORN1"
    if not a and not r:
        return "RIVER_CORN2"
    if not b and not l:
        return "RIVER_CORN3"
    if not b and not r:
        return "RIVER_CORN4"
    if not b and not a:
        return "RIVER_SIDE1"
    if not l and not r:
        return "RIVER_SIDE2"
    if not l:
        return "RIVER_ONESIDE1"
    if not b:
        return "RIVER_ONESIDE2"
    if not r:
        return "RIVER_ONESIDE3"
    if not a:
        return "RIVER_ONESIDE4"
    return "RIVER_SOLID"


def calc_deep(n):
    n = [RIVER if t == BOAT else t for t in n]
    al, a, ar, l, r, bl, b, br = n
    if al != DEEP and a != DEEP and l != DEEP and r == DEEP and b == DEEP:
        return "DEEP_CORN1"
    if ar != DEEP and a != DEEP and r != DEEP and l == DEEP and b == DEEP:
        return "DEEP_CORN2"
    if br != DEEP and b != DEEP and r != DEEP and l == DEEP and a == DEEP:
        return "DEEP_CORN4"
    if bl != DEEP and b != DEEP and l != DEEP and r == DEEP and a == DEEP:
        return "DEEP_CORN3"
    if l == RIVER and r == DEEP:
        return "DEEP_SIDE1"
    if b == RIVER and a == DEEP:
        return "DEEP_SIDE2"
    if a == RIVER and b == DEEP:
        return "DEEP_SIDE3"
    if r == RIVER and l == DEEP:
        return "DEEP_SIDE4"
    return "DEEP_SOLID"


def calc_forest(n):
    al, a, ar, l, r, bl, b, br = [t == FOREST for t in n]
    if not a and not l and r and b:
        return "FOREST_BR"
    if not a and l and not r and b:
        return "FOREST_BL"
    if a and l and not r and not b:
        return "FOREST_AL"
    if a and not l and r and not b:
        return "FOREST_AR"
    if a and not l and not r and not b:
        return "FOREST_ABOVE"
    if not a and not l and not r and b:
        return "FOREST_BELOW"
    if not a and l and not r and not b:
        return "FOREST_LEFT"
    if not a and not l and r and not b:
        return "FOREST_RIGHT"
    if not a and not b and not l and not r:
        return "FOREST_SINGLE"
    return "FOREST"


ROAD_NAMES = ["ROAD_HORZ", "ROAD_VERT"] + ["ROAD_CORNER%d" % i for i in range(1, 9)] + \
    ["ROAD_SOLID", "ROAD_CROSS"] + ["ROAD_T%d" % i for i in range(1, 5)] + \
    ["ROAD_WATER%d" % i for i in range(1, 12)] + ["ROAD_SIDE%d" % i for i in range(1, 5)]
BUILD_NAMES = ["BUILD_SINGLE", "BUILD_HORZ", "BUILD_VERT", "BUILD_HORZEND1", "BUILD_HORZEND2",
               "BUILD_VERTEND1", "BUILD_VERTEND2", "BUILD_SOLID"] + \
    ["BUILD_CORNER%d" % i for i in range(1, 5)] + ["BUILD_L%d" % i for i in range(1, 5)] + \
    ["BUILD_T%d" % i for i in range(1, 5)] + ["BUILD_CROSS"] + \
    ["BUILD_SIDE%d" % i for i in range(1, 5)] + ["BUILD_SIDECORN%d" % i for i in range(1, 17)] + \
    ["BUILD_TWIST1", "BUILD_TWIST2"] + ["BUILD_MOST%d" % i for i in range(1, 5)]
RIVER_NAMES = ["RIVER_END1", "RIVER_END2", "RIVER_END3", "RIVER_END4", "RIVER_SOLID",
               "RIVER_SURROUND", "RIVER_SIDE1", "RIVER_SIDE2"] + \
    ["RIVER_ONESIDE%d" % i for i in range(1, 5)] + ["RIVER_CORN%d" % i for i in range(1, 5)]
DEEP_NAMES = ["DEEP_SOLID"] + ["DEEP_CORN%d" % i for i in range(1, 5)] + \
    ["DEEP_SIDE%d" % i for i in range(1, 5)]
FOREST_NAMES = ["FOREST", "FOREST_SINGLE", "FOREST_BR", "FOREST_BL", "FOREST_AL", "FOREST_AR",
                "FOREST_ABOVE", "FOREST_BELOW", "FOREST_LEFT", "FOREST_RIGHT"]
BOAT_NAMES = ["BOAT_%d" % i for i in range(8)]
PILL_NAMES = ["PILL_%s_%d" % (o, d) for o in ("OWN", "ALLY", "ENEMY") for d in range(4)] + ["PILL_DEAD"]
BASE_NAMES = ["BASE_OWN", "BASE_ALLY", "BASE_ENEMY", "BASE_NEUTRAL"]
TERRAIN = ["GRASS", "SWAMP", "CRATER", "RUBBLE", "HALFBUILDING", "START"] + ROAD_NAMES + \
    BUILD_NAMES + RIVER_NAMES + DEEP_NAMES + FOREST_NAMES + BOAT_NAMES + PILL_NAMES + BASE_NAMES

OWNERS = ("OWN", "ALLY", "ENEMY")
# The button bar and status panel icons: opaque pictures on the panel
# colour, drawn again on the highlight colour for the chosen action. Each
# icon has its own 8 and 16 pixel grid. Letters are materials (see
# ICON_MATS), "." the ground. They are outlined so they read at 1 and 2
# bits as well as in colour.
ICON_NAMES = ["TREE", "ROAD", "WALL", "PILL", "MINE", "NEAR", "FAR"]
# More icons for the status panel, without a highlighted twin.
ICON_EXTRA = ["SHELL", "ARMOUR", "MANIN", "MANOUT", "MANBACK", "PARA"]
ICON_MATS = {"k": "outline", "w": "white", "g": "forest", "G": "forest_hi", "D": "forest_sh",
             "T": "bld_sh", "t": "bld_roof", "b": "bld_roof", "B": "bld_roof2", "H": "bld_hi",
             "s": "bld_sh", "r": "road", "l": "road_line", "K": "kerb", "c": "concrete",
             "C": "concrete_hi", "x": "concrete_sh", "o": "own", "O": "icon_own", "a": "ally",
             "A": "ally_sh", "e": "enemy", "E": "enemy_sh", "n": "man_skin", "p": "man_suit",
             "q": "para_a", "P": "para_b", "Y": "shell", "y": "expl_y", "f": "expl_o",
             "R": "expl_r", "m": "mine_body", "h": "mine_hi", "u": "tread", "U": "tread_hi",
             "z": "barrel", "j": "tank_own", "J": "tank_own_sh"}
ICON8 = {
    "TREE": ["..kkkk..",
             ".kGGGGk.",
             "kGGGggDk",
             "kGgggDDk",
             ".kDDDDk.",
             "..kTTk..",
             "...TT...",
             "..TTTT.."],
    "ROAD": ["........",
             "........",
             "kkkkkkkk",
             "rrrrrrrr",
             "rllrrllr",
             "rrrrrrrr",
             "kkkkkkkk",
             "........"],
    "WALL": ["kkkkkkkk",
             "kBBBkBBk",
             "kBBBkBBk",
             "kkkkkkkk",
             "kBkBBBBk",
             "kBkBBBBk",
             "kkkkkkkk",
             "........"],
    "PILL": ["..kkkk..",
             ".kCccck.",
             "kCckkcxk",
             "kckOOkxk",
             "kckOOkxk",
             "kcckkxxk",
             ".kxxxxk.",
             "..kkkk.."],
    "MINE": ["...kk...",
             ".k.kk.k.",
             "..kmmk..",
             "kkmhmmkk",
             "kkmmmmkk",
             "..kmmk..",
             ".k.kk.k.",
             "...kk..."],
    "NEAR": ["...kk...",
             ".kk..kk.",
             "...kk...",
             "...ee...",
             "...ee...",
             ".eeeeee.",
             "..eeee..",
             "...ee..."],
    "FAR": ["...kk...",
            ".kk..kk.",
            "...kk...",
            "...ee...",
            "..eeee..",
            ".eeeeee.",
            "...ee...",
            "...ee..."],
    "SHELL": ["...kk...",
              "..kfRk..",
              "..kfRk..",
              "..kkkk..",
              "..kYyk..",
              "..kYyk..",
              "..kYyk..",
              ".kkkkkk."],
    "ARMOUR": ["kkkkkkkk",
               "kaawAAAk",
               "kaawAAAk",
               "kaawAAAk",
               ".kawAAk.",
               ".kawAAk.",
               "..kwAk..",
               "...kk..."],
    "MANIN": ["..knnk..",
              "..kppk..",
              ".kjjjJzz",
              "kkkkkkkk",
              "kjjjjjJk",
              "kkkkkkkk",
              "kuUuUuUk",
              ".kkkkkk."],
    "MANOUT": [".kkk....",
               ".knk.e..",
               "kpppkee.",
               "kpppkeee",
               ".kpk.ee.",
               ".kpk.e..",
               ".k.k....",
               "kk.kk..."],
    "PARA": ["..kkkk..",
             ".kPqqPk.",
             "kPPqqPPk",
             "kkkkkkkk",
             ".k....k.",
             "..knnk..",
             "..kppk..",
             "..k..k.."],
}
ICON16 = {
    "TREE": ["......kkkk......",
             "....kkGGggkk....",
             "...kGGGgggggk...",
             "..kGGgggggggDk..",
             ".kGGgggGGgggDDk.",
             ".kGgggGGggggDDk.",
             ".kgggggggggDDDk.",
             ".kggGgggggDDDDk.",
             "..kgggggDDDDDk..",
             "..kkDgggDDDDkk..",
             "....kkDDDDkk....",
             "......kTtk......",
             "......kTtk......",
             "......kTtk......",
             ".....kTTttk.....",
             "................"],
    "ROAD": ["................",
             "................",
             "kkkkkkkkkkkkkkkk",
             "KKKKKKKKKKKKKKKK",
             "rrrrrrrrrrrrrrrr",
             "rrrrrrrrrrrrrrrr",
             "rrrrrrrrrrrrrrrr",
             "rrllllrrrrllllrr",
             "rrllllrrrrllllrr",
             "rrrrrrrrrrrrrrrr",
             "rrrrrrrrrrrrrrrr",
             "rrrrrrrrrrrrrrrr",
             "KKKKKKKKKKKKKKKK",
             "kkkkkkkkkkkkkkkk",
             "................",
             "................"],
    "WALL": ["................",
             "kkkkkkkkkkkkkkkk",
             "kHBBBBBkHBBBBBBk",
             "kBbbbbskBbbbbbsk",
             "kBbbbbskBbbbbbsk",
             "kssssssksssssssk",
             "kkkkkkkkkkkkkkkk",
             "kBBkHBBBBBBkHBBk",
             "kbskBbbbbbskBbbk",
             "kbskBbbbbbskBbbk",
             "kssksssssssksssk",
             "kkkkkkkkkkkkkkkk",
             "kHBBBBBkHBBBBBBk",
             "kBbbbbskBbbbbbsk",
             "kssssssksssssssk",
             "kkkkkkkkkkkkkkkk"],
    "PILL": ["................",
             ".....kkkkkk.....",
             "...kkCCcccckk...",
             "..kCCccccccxxk..",
             "..kCcckkkkcxxk..",
             ".kCcckOOOOkcxxk.",
             ".kCckOoooOkcxxk.",
             ".kcckOoooOkcxxk.",
             ".kcckOooOOkcxxk.",
             ".kccckOOOOkxxxk.",
             "..kccckkkkxxxk..",
             "..kccccccxxxxk..",
             "...kkcxxxxxkk...",
             ".....kkkkkk.....",
             "................",
             "................"],
    "MINE": ["................",
             ".......kk.......",
             "..k....kk....k..",
             "...k.kkkkkk.k...",
             "....kmmmmmmk....",
             "...kmhhmmmmmk...",
             "...kmhmmmmmmk...",
             ".kkkmmmmmmmmkkk.",
             ".kkkmmmmmmmxkkk.",
             "...kmmmmmmxxk...",
             "...kmmmmmxxxk...",
             "....kmmxxxxk....",
             "...k.kkkkkk.k...",
             "..k....kk....k..",
             ".......kk.......",
             "................"],
    "NEAR": [".....kkkkkk.....",
             "....k..kk..k....",
             "...k........k...",
             "...kkk.ee.kkk...",
             "...k........k...",
             "....k..kk..k....",
             ".....kkkkkk.....",
             "................",
             "......keek......",
             "......keek......",
             "......keek......",
             "..kkkkkeekkkkk..",
             "...keeeeeeeEk...",
             "....keeeeeEk....",
             ".....keeeEk.....",
             "......kkkk......"],
    "FAR": [".....kkkkkk.....",
            "....k..kk..k....",
            "...k........k...",
            "...kkk.ee.kkk...",
            "...k........k...",
            "....k..kk..k....",
            ".....kkkkkk.....",
            "......kkkk......",
            ".....keeeEk.....",
            "....keeeeeEk....",
            "...keeeeeeeEk...",
            "..kkkkkeekkkkk..",
            "......keek......",
            "......keek......",
            "......keek......",
            "......kkkk......"],
    "SHELL": ["......kkkk......",
              ".....kfffRk.....",
              "....kffffRRk....",
              "....kwfffRRk....",
              "....kwffffRk....",
              "....kkkkkkkk....",
              "....kYyyyybk....",
              "....kYyyyybk....",
              "....kYyyyybk....",
              "....kYyyyybk....",
              "....kYyyyybk....",
              "....kYyyyybk....",
              "....kkkkkkkk....",
              "...kbbbbbbbbk...",
              "...kkkkkkkkkk...",
              "................"],
    "ARMOUR": ["................",
               ".kkkkkkkkkkkkkk.",
               ".kwwwwwwwaaaaAk.",
               ".kwaaaaawAAAAAk.",
               ".kwaaaaawAAAAAk.",
               ".kwaaaaawAAAAAk.",
               ".kwaaaaawAAAAAk.",
               ".kwaaaaawAAAAAk.",
               "..kaaaaawAAAAk..",
               "..kaaaaawAAAAk..",
               "...kaaaawAAAk...",
               "....kaaawAAk....",
               ".....kaawAk.....",
               "......kawk......",
               ".......kk.......",
               "................"],
    "MANIN": ["................",
              ".......kk.......",
              "......knnk......",
              "......knnk......",
              "....kkkppkkk....",
              "...kjjjjjjJkkkkk",
              "...kjjjjjjJzzzzk",
              "...kjjjjjjJkkkkk",
              ".kkkkkkkkkkkkkk.",
              "kjjjjjjjjjjjjjJk",
              "kJJJJJJJJJJJJJJk",
              "kkkkkkkkkkkkkkkk",
              "kuuuuuuuuuuuuuuk",
              "kuUkuUkuUkuUkuuk",
              ".kkkkkkkkkkkkkk.",
              "................"],
    "MANOUT": ["................",
               "...kkkk.........",
               "..knnnnk........",
               "..knnnnk........",
               "...kkkk.........",
               ".kkppppkk.k.....",
               "kppppppppkek....",
               "kpkppppkpkeek...",
               "knkppppknkeeek..",
               "kk.kppk.kkeeeek.",
               "...kppk..keeek..",
               "..kpkkpk.keek...",
               "..kpk.kpkkek....",
               ".kpk...kpkk.....",
               ".kk.....kk......",
               "................"],
    "PARA": ["....kkkkkkkk....",
             "...kPqqPPqqPk...",
             "..kPqqqPPqqqPk..",
             ".kPPqqqPPqqqPPk.",
             ".kPPqqqPPqqqPPk.",
             "kPPPqqqPPqqqPPPk",
             "kkkkkkkkkkkkkkkk",
             ".k....k..k....k.",
             "..k...k..k...k..",
             "...k..knnk..k...",
             "....kkknnkkk....",
             ".....kppppk.....",
             ".....kppppk.....",
             "......kppk......",
             ".....kk..kk.....",
             "................"],
}


def icon_grid(name, s):
    """The grid of an icon at size s. Coming back is going out mirrored."""
    g = (ICON8 if s == 8 else ICON16)["MANOUT" if name == "MANBACK" else name]
    assert len(g) == s and all(len(r) == s for r in g), "%s %d is not %d square" % (name, s, s)
    return [r[::-1] for r in g] if name == "MANBACK" else g


def draw_icon(s, name, sel):
    c = Cell(s)
    bg = "sel" if sel else "concrete_hi"
    for y, row in enumerate(icon_grid(name, s)):
        for x, ch in enumerate(row):
            c.set(x, y, bg if ch == "." else ICON_MATS[ch])
    return c


SPRITES = ["TANK_%s%s_%d" % (o, b, d) for o in OWNERS for b in ("", "_BOAT") for d in range(16)] + \
    ["MAN_0", "MAN_1", "MAN_2", "PARA"] + ["SHELL_%d" % d for d in range(16)] + \
    ["EXPL_%d" % i for i in range(8)] + ["MINE"] + \
    ["ICON_" + n for n in ICON_NAMES] + ["ICON_%s_SEL" % n for n in ICON_NAMES] + \
    ["ICON_" + n for n in ICON_EXTRA]


def combos(alphabet):
    """Every neighbourhood over the alphabet (one for all eight, or a list of
    eight), as 8-tuples."""
    if not isinstance(alphabet[0], tuple):
        alphabet = [alphabet] * 8
    out = [()]
    for i in range(8):
        out = [c + (t,) for c in out for t in alphabet[i]]
    return out


# ---------------------------------------------------------------------------
# Drawing. A cell is a list of rows of material names.

class Cell:
    def __init__(self, s, fill="clear"):
        self.s = s
        self.p = [[fill] * s for _ in range(s)]

    def get(self, x, y):
        return self.p[y][x]

    def set(self, x, y, m):
        if 0 <= x < self.s and 0 <= y < self.s:
            self.p[y][x] = m

    def key(self):
        return tuple(tuple(r) for r in self.p)


def hash2(x, y, seed=0):
    h = (x * 374761393 + y * 668265263 + seed * 2246822519) & 0xFFFFFFFF
    h = ((h ^ (h >> 13)) * 1274126177) & 0xFFFFFFFF
    return (h ^ (h >> 16)) & 0xFFFF


# Grass flecks: little tufts, the same in every grass-like square so ground
# around roads, rivers and pills joins up with grass squares.
GRASS8 = {(1, 1): "grass_f", (5, 2): "grass_f", (3, 5): "grass_f", (7, 6): "grass_f",
          (6, 2): "grass_f2", (4, 5): "grass_f2"}
GRASS16 = {}
for (x, y) in [(2, 2), (10, 1), (6, 6), (14, 7), (2, 11), (9, 12), (13, 14), (5, 15)]:
    GRASS16[(x, y)] = "grass_f"
    GRASS16[(x + 1, y - 1)] = "grass_f2"
    GRASS16[(x - 1, y - 1)] = "grass_f"


def grass_at(s, x, y):
    tbl = GRASS8 if s == 8 else GRASS16
    return tbl.get((x % s, y % s), "grass")


def draw_grass(s):
    c = Cell(s)
    for y in range(s):
        for x in range(s):
            c.set(x, y, grass_at(s, x, y))
    return c


def draw_swamp(s):
    c = Cell(s, "swamp")
    if s == 8:
        water = [(0, 2), (1, 2), (2, 2), (5, 5), (6, 5), (4, 0), (5, 0)]
        reeds = [(6, 1), (6, 2), (7, 2), (2, 5), (2, 6), (1, 6)]
    else:
        water = []
        for (x, y, w) in [(1, 4, 5), (2, 5, 3), (9, 10, 6), (10, 11, 4), (10, 1, 4), (3, 13, 3)]:
            water += [(x + i, y) for i in range(w)]
        reeds = []
        for (x, y) in [(12, 4), (5, 9), (14, 13), (1, 1)]:
            reeds += [(x, y), (x, y + 1), (x + 1, y + 1), (x - 1, y), (x + 1, y - 1)]
    for p in water:
        c.set(p[0], p[1], "swamp_water")
    for p in reeds:
        c.set(p[0], p[1], "swamp_reed")
    return c


def draw_crater(s):
    # A round hole in bare earth, lit from the upper left: the far (upper
    # left) wall inside is in shadow, the near wall and the outer lip at the
    # upper left catch the light.
    c = Cell(s, "dirt")
    mid = s / 2
    r = s * 0.42
    for y in range(s):
        for x in range(s):
            dx, dy = x + 0.5 - mid, y + 0.5 - mid
            d = math.hypot(dx, dy)
            if d <= r:
                hole = math.hypot(dx + s * 0.08, dy + s * 0.08) <= r * 0.72
                c.set(x, y, "hole" if hole else "dirt_hi" if dx + dy > 0 else "dirt")
            elif d <= r + (1 if s == 8 else 1.5) and dx + dy < 0:
                c.set(x, y, "dirt_hi")
    for (x, y) in ([(0, 7), (7, 0)] if s == 8 else [(1, 14), (14, 1), (15, 15), (0, 0)]):
        c.set(x, y, "dirt_hi")
    return c


def draw_rubble(s, base=None):
    c = base or Cell(s, "dirt")
    if s == 8:
        chunks = [(1, 1, 2, 1), (5, 0, 2, 2), (3, 3, 2, 2), (0, 5, 2, 2), (6, 5, 1, 2), (3, 6, 2, 1)]
    else:
        chunks = [(1, 2, 4, 2), (9, 1, 4, 3), (6, 6, 4, 4), (12, 7, 3, 3), (1, 10, 4, 3),
                  (8, 12, 4, 2), (13, 13, 2, 2), (4, 15, 2, 1)]
    for (x, y, w, h) in chunks:
        for j in range(h):
            for i in range(w):
                m = "bld_roof2"
                if j == 0 and i < w - 1:
                    m = "bld_hi"
                elif j == h - 1 or i == w - 1:
                    m = "bld_sh"
                c.set(x + i, y + j, m)
    return c


def water_at(s, x, y, deep=False):
    """River or deep sea texture: the base with a few ripples."""
    if s == 8:
        rip = [(1, 1), (2, 1), (5, 4), (6, 4)] if not deep else [(2, 2), (3, 2), (6, 6), (7, 6)]
    else:
        rip = [(2, 2), (3, 2), (4, 2), (1, 3), (10, 6), (11, 6), (12, 6), (9, 7),
               (5, 11), (6, 11), (7, 11), (4, 12), (13, 14), (14, 14), (12, 15)]
        if deep:
            rip = [(x2, (y2 + 1) % 16) for (x2, y2) in rip[:4] + rip[8:]]
    if (x % s, y % s) in rip:
        return "deep_hi" if deep else "river_hi"
    return "deep" if deep else "river"


# Shapes from connectivity. conn[side] for N, S, W, E and the diagonals.
# A point (in tile pixels, 0..s) is inside if it is in the middle box, an
# arm towards a connected side, or a corner box whose two sides and the
# diagonal are connected (or just the two sides when diag is None).

def shape_inside(s, m, conn, px, py, radius=0.0, diag=True):
    n, so, w, e = conn["N"], conn["S"], conn["W"], conn["E"]
    lo, hi = m, s - m
    inx = lo <= px <= hi
    iny = lo <= py <= hi
    if inx and iny:
        # Round the corners of the middle box where both sides are open.
        if radius > 0:
            for (cxo, cyo, a, b) in ((lo, lo, n, w), (hi, lo, n, e), (lo, hi, so, w), (hi, hi, so, e)):
                if a or b:
                    continue
                ccx = cxo + (radius if cxo == lo else -radius)
                ccy = cyo + (radius if cyo == lo else -radius)
                if (px - ccx) * (cxo - ccx) > 0 and (py - ccy) * (cyo - ccy) > 0:
                    if math.hypot(px - ccx, py - ccy) > radius:
                        return False
        return True
    if inx and py < lo:
        return n
    if inx and py > hi:
        return so
    if iny and px < lo:
        return w
    if iny and px > hi:
        return e
    # Corner boxes.
    a = n if py < lo else so
    b = w if px < lo else e
    key = ("N" if py < lo else "S") + ("W" if px < lo else "E")
    if not (a and b):
        return False
    if diag:
        return conn[key]
    return True


def conn_from(n, test):
    return {"N": test(n[A]), "S": test(n[B]), "W": test(n[L]), "E": test(n[R]),
            "NW": test(n[AL]), "NE": test(n[AR]), "SW": test(n[BL]), "SE": test(n[BR])}


def mask_cell(s, m, conn, radius=0.0, diag=True):
    return [[shape_inside(s, m, conn, x + 0.5, y + 0.5, radius, diag) for x in range(s)]
            for y in range(s)]


def mask_at(mask, s, conn, x, y):
    """The mask, with points outside the cell counted by connectivity."""
    if 0 <= x < s and 0 <= y < s:
        return mask[y][x]
    sx = "W" if x < 0 else "E" if x >= s else ""
    sy = "N" if y < 0 else "S" if y >= s else ""
    if sx and sy:
        return conn[sy + sx] and conn[sy] and conn[sx]
    return conn[sx or sy]


def draw_building(s, n):
    conn = conn_from(n, lambda t: t in (BUILDING, HALF))
    m = 1 if s == 8 else 2
    mask = mask_cell(s, m, conn)
    c = Cell(s)
    for y in range(s):
        for x in range(s):
            if not mask[y][x]:
                c.set(x, y, grass_at(s, x, y))
                continue

            def ins(dx, dy):
                return mask_at(mask, s, conn, x + dx, y + dy)
            lit = not ins(-1, 0) or not ins(0, -1)
            dark = not ins(1, 0) or not ins(0, 1)
            if s == 8:
                # No room for an outline: lit edges top and left, dark
                # edges bottom and right.
                if dark:
                    c.set(x, y, "bld_line")
                elif lit or not ins(-1, -1):
                    c.set(x, y, "bld_hi")
                elif not ins(1, 1) or not ins(2, 0) or not ins(0, 2):
                    c.set(x, y, "bld_sh")
                else:
                    c.set(x, y, "bld_roof" if (x + y) % 2 else "bld_roof2")
                continue
            if lit or dark:
                c.set(x, y, "bld_line")
            elif not ins(-1, -1) or not ins(0, -2) or not ins(-2, 0):
                c.set(x, y, "bld_hi")
            elif not ins(1, 1) or not ins(0, 2) or not ins(2, 0):
                c.set(x, y, "bld_sh")
            else:
                c.set(x, y, "bld_roof" if (x + y) % 2 else "bld_roof2")
    return c


def draw_half(s):
    # A broken building: the block with its lower right part fallen in.
    c = draw_building(s, [GRASS] * 8)
    for y in range(s):
        for x in range(s):
            if x + y > s * 1.05 - (2 if (x * 7 + y * 3) % 5 == 0 else 0):
                c.set(x, y, "dirt")
    chunks = [(5, 6), (6, 5), (4, 7), (7, 4)] if s == 8 else \
        [(10, 12), (11, 12), (12, 10), (13, 10), (13, 11), (8, 14), (9, 14), (14, 8), (14, 7), (12, 13)]
    for i, (x, y) in enumerate(chunks):
        c.set(x, y, "bld_roof2" if i % 2 == 0 else "bld_sh")
    return c


# Tree crowns, drawn on a brick lattice so they tile. h light, f mid,
# s shadow, "," the dark gap between crowns, "." nothing.
TREE8 = [",hf,",
         "hhfs",
         "hfss",
         ",ss,"]
TREE16 = [",,hhf,,,",
          ",hhhff,,",
          "hhhffff,",
          "hhffffs,",
          "hfffffs,",
          ",fffss,,",
          ",,sss,,,",
          ",,,,,,,,"]
TREE_MAT = {"h": "forest_hi", "f": "forest", "s": "forest_sh"}


def draw_forest(s, n):
    conn = conn_from(n, lambda t: t == FOREST)
    for k in ("NW", "NE", "SW", "SE"):
        conn[k] = conn[k[0]] and conn[k[1]]
    if s == 8:
        lattice, glyph, mrg, edge = [(2, 2), (6, 2), (0, 6), (4, 6)], TREE8, 1.5, 1
    else:
        lattice, glyph, mrg, edge = [(4, 4), (12, 4), (0, 12), (8, 12)], TREE16, 3, 2
    g = len(glyph)
    trees = []
    for oy in (-1, 0, 1):
        for ox in (-1, 0, 1):
            for (lx, ly) in lattice:
                cx, cy = lx + ox * s, ly + oy * s
                # The square the centre is in; a centre on the line between
                # two squares belongs to the one right or below.
                tx = -1 if cx < 0 else 1 if cx >= s else 0
                ty = -1 if cy < 0 else 1 if cy >= s else 0
                if (tx, ty) == (0, 0):
                    ok = (conn["W"] or cx >= mrg) and (conn["E"] or cx <= s - mrg) and \
                         (conn["N"] or cy >= mrg) and (conn["S"] or cy <= s - mrg)
                else:
                    key = ("N" if ty < 0 else "S" if ty > 0 else "") + ("W" if tx < 0 else "E" if tx > 0 else "")
                    ok = conn[key]
                if ok:
                    trees.append((cx - g // 2, cy - g // 2))
    area = mask_cell(s, edge, conn, radius=s * 0.3)
    c = Cell(s)
    for y in range(s):
        for x in range(s):
            c.set(x, y, grass_at(s, x, y))
    # Gaps first, then crowns, upper trees first so lower ones overlap.
    for gaps in (True, False):
        for (ox, oy) in sorted(trees, key=lambda p: (p[1], p[0])):
            for j in range(g):
                for i in range(g):
                    ch = glyph[j][i]
                    x, y = ox + i, oy + j
                    if not (0 <= x < s and 0 <= y < s) or ch == "." or not area[y][x]:
                        continue
                    if gaps and ch == ",":
                        c.set(x, y, "forest_sh")
                    elif not gaps and ch != ",":
                        c.set(x, y, TREE_MAT[ch])
    return c


def draw_river(s, n):
    conn = conn_from(n, lambda t: t in (RIVER, ROAD, DEEP, BOAT))
    m = 1 if s == 8 else 2
    mask = mask_cell(s, m, conn, radius=s * 0.3, diag=False)
    c = Cell(s)
    for y in range(s):
        for x in range(s):
            if not mask[y][x]:
                c.set(x, y, grass_at(s, x, y))
                continue
            def ins(dx, dy):
                return mask_at(mask, s, {**conn, "NW": True, "NE": True, "SW": True, "SE": True}, x + dx, y + dy)
            if not (ins(-1, 0) and ins(1, 0) and ins(0, -1) and ins(0, 1)):
                c.set(x, y, "river_dark")
            else:
                c.set(x, y, water_at(s, x, y))
    return c


def draw_deep(s, n):
    conn = conn_from(n, lambda t: t == DEEP)
    m = 2 if s == 8 else 4
    mask = mask_cell(s, m, conn, radius=s * 0.35)
    c = Cell(s)
    for y in range(s):
        for x in range(s):
            if not mask[y][x]:
                c.set(x, y, water_at(s, x, y))
                continue
            def ins(dx, dy):
                return mask_at(mask, s, conn, x + dx, y + dy)
            edge = not (ins(-1, 0) and ins(1, 0) and ins(0, -1) and ins(0, 1))
            if edge:
                c.set(x, y, "deep_hi")
            else:
                c.set(x, y, water_at(s, x, y, deep=True))
    return c


def draw_road(s, n):
    roadc = conn_from(n, lambda t: t == ROAD)
    water = conn_from(n, lambda t: t in (RIVER, DEEP, BOAT))
    m = 1 if s == 8 else 2
    mask = mask_cell(s, m, roadc, radius=s * 0.3)
    wide = any(roadc[k] and roadc[k[0]] and roadc[k[1]] for k in ("NW", "NE", "SW", "SE"))
    c = Cell(s)
    # Which side a pixel outside the road lies on, for water or ground.
    def side_of(x, y):
        dx, dy = x + 0.5 - s / 2, y + 0.5 - s / 2
        if abs(dx) > abs(dy):
            return "W" if dx < 0 else "E"
        return "N" if dy < 0 else "S"
    for y in range(s):
        for x in range(s):
            if not mask[y][x]:
                c.set(x, y, water_at(s, x, y) if water[side_of(x, y)] else grass_at(s, x, y))
                continue
            edge = None
            for (dx, dy) in ((-1, 0), (1, 0), (0, -1), (0, 1)):
                if not mask_at(mask, s, roadc, x + dx, y + dy):
                    edge = side_of(x + dx, y + dy) if 0 <= x + dx < s and 0 <= y + dy < s else None
                    edge = edge or ("W" if dx < 0 else "E" if dx > 0 else "N" if dy < 0 else "S")
            if edge:
                if water[edge]:
                    post = (x + y) % (3 if s == 8 else 4) == 0
                    c.set(x, y, "rail_post" if post else "rail")
                else:
                    c.set(x, y, "kerb")
                continue
            c.set(x, y, "road")
    if not wide:
        # Dashes down the middle of each arm, in step from square to square.
        h = s // 2 - (1 if s == 8 else 0)
        mid = [h] if s == 8 else [7, 8]
        for i in range(s):
            on = (i % 4) in (1, 2) if s == 8 else (i % 8) in (2, 3, 4, 5)
            if not on:
                continue
            for j in mid:
                if (roadc["W"] and i < s / 2) or (roadc["E"] and i >= s / 2):
                    if c.get(i, j) == "road":
                        c.set(i, j, "road_line")
                if (roadc["N"] and i < s / 2) or (roadc["S"] and i >= s / 2):
                    if c.get(j, i) == "road":
                        c.set(j, i, "road_line")
    return c


# Polygons, drawn by sampling each pixel at 4 x 4 points.

def poly_inside(pts, x, y):
    inside = False
    j = len(pts) - 1
    for i in range(len(pts)):
        xi, yi = pts[i]
        xj, yj = pts[j]
        if (yi > y) != (yj > y) and x < (xj - xi) * (y - yi) / (yj - yi) + xi:
            inside = not inside
        j = i
    return inside


def rotate(pts, ang, cx, cy, scale=1.0):
    ca, sa = math.cos(ang), math.sin(ang)
    return [(cx + (x * ca - y * sa) * scale, cy + (x * sa + y * ca) * scale) for (x, y) in pts]


def rasterise(c, layers, thresh=0.5, sub=4):
    """layers: (material, shape) drawn in order, shape(x, y) -> bool, or a
    function of the sub-pixel point returning a material name or None."""
    s = c.s
    for y in range(s):
        for x in range(s):
            votes = Counter()
            for j in range(sub):
                for i in range(sub):
                    px, py = x + (i + 0.5) / sub, y + (j + 0.5) / sub
                    m = None
                    for (mat, shape) in layers:
                        if shape(px, py):
                            m = mat(px, py) if callable(mat) else mat
                    if m:
                        votes[m] += 1
            if votes:
                mat, k = max(votes.items(), key=lambda kv: (kv[1], kv[0]))
                if sum(votes.values()) >= thresh * sub * sub:
                    # Thin parts (the barrel) win if they cover enough.
                    c.set(x, y, mat)
    return c


def outline(c, mat="outline", diag=False):
    s = c.s
    add = []
    for y in range(s):
        for x in range(s):
            if c.get(x, y) != "clear":
                continue
            nb = [(-1, 0), (1, 0), (0, -1), (0, 1)]
            if diag:
                nb += [(-1, -1), (1, -1), (-1, 1), (1, 1)]
            for (dx, dy) in nb:
                if 0 <= x + dx < s and 0 <= y + dy < s and c.get(x + dx, y + dy) not in ("clear", mat):
                    add.append((x, y))
                    break
    for (x, y) in add:
        c.set(x, y, mat)
    return c


def boat_poly(s):
    k = s / 16.0
    return [(x * k, y * k) for (x, y) in [(0, -7.6), (3.2, -4.5), (4.0, -1.5), (4.0, 6.6),
                                          (-4.0, 6.6), (-4.0, -1.5), (-3.2, -4.5)]]


def draw_boat_shape(c, ang, scale=1.0, deck=True):
    s = c.s
    hull = rotate(boat_poly(s), ang, s / 2, s / 2, scale)
    k = s / 16.0 * scale
    inner = rotate([(x * 0.55, y * 0.6 + 0.6 * k) for (x, y) in boat_poly(s)], ang, s / 2, s / 2, scale)
    layers = [("boat", lambda x, y: poly_inside(hull, x, y))]
    if deck and s == 16:
        layers.append(("boat_hi", lambda x, y: poly_inside(inner, x, y)))
    rasterise(c, layers)
    return c


def draw_boat_tile(s, name):
    # Bow points away from the land: BOAT_0 land below, 2 left, 3 right,
    # 4 below and left, 5 above and left, 6 above and right, 7 below and
    # right, 1 open water.
    heading = {"BOAT_0": 0, "BOAT_1": 4, "BOAT_2": 2, "BOAT_3": 6,
               "BOAT_4": 1, "BOAT_5": 3, "BOAT_6": 5, "BOAT_7": 7}[name]
    sp = Cell(s)
    draw_boat_shape(sp, heading * math.pi / 4)
    outline(sp)
    c = Cell(s)
    for y in range(s):
        for x in range(s):
            m = sp.get(x, y)
            c.set(x, y, m if m != "clear" else water_at(s, x, y))
    return c


def octagon(s, inset, cut):
    a, b = inset, s - inset
    return [(a + cut, a), (b - cut, a), (b, a + cut), (b, b - cut), (b - cut, b), (a + cut, b),
            (a, b - cut), (a, a + cut)]


def draw_pill(s, owner, dmg):
    c = Cell(s)
    for y in range(s):
        for x in range(s):
            c.set(x, y, grass_at(s, x, y))
    if owner is None:
        # Destroyed: a heap of broken concrete.
        heap = octagon(s, s * 0.12, s * 0.25)
        rasterise(c, [("concrete_sh", lambda x, y: poly_inside(heap, x, y))])
        bits = [(2, 2), (5, 3), (3, 5), (4, 4)] if s == 8 else \
            [(4, 4), (5, 4), (10, 5), (11, 6), (6, 10), (7, 10), (9, 9), (8, 8), (11, 11), (4, 8)]
        for (x, y) in bits:
            c.set(x, y, "concrete")
        return c
    col = {"OWN": ("own", "own_sh"), "ALLY": ("ally", "ally_sh"), "ENEMY": ("enemy", "enemy_sh")}[owner]
    if s == 8:
        return pill8(c, col, dmg)
    body = octagon(s, 0, s * 0.28)
    turret_r = s * 0.2
    mid = s / 2

    def concrete(x, y):
        d = (x - mid) + (y - mid)
        return "concrete_hi" if d < -s * 0.35 else "concrete_sh" if d > s * 0.35 else "concrete"

    def turret(x, y):
        return col[1] if (x - mid) + (y - mid) > turret_r * 0.6 else col[0]
    rasterise(c, [(concrete, lambda x, y: poly_inside(body, x, y)),
                  ("outline", lambda x, y: math.hypot(x - mid, y - mid) <= turret_r + (1.0 if s == 8 else 1.2)),
                  (turret, lambda x, y: math.hypot(x - mid, y - mid) <= turret_r)])
    # Outline round the octagon, inside the square.
    for y in range(s):
        for x in range(s):
            if c.get(x, y).startswith("concrete"):
                for (dx, dy) in ((-1, 0), (1, 0), (0, -1), (0, 1)):
                    xx, yy = x + dx, y + dy
                    if not (0 <= xx < s and 0 <= yy < s) or c.get(xx, yy).startswith("grass"):
                        c.set(x, y, "outline")
                        break
    # Damage: cracks, then chunks gone.
    if s == 8:
        cracks = [[], [(2, 1), (1, 2)], [(2, 1), (1, 2), (6, 5), (5, 6), (6, 2)],
                  [(2, 1), (1, 2), (6, 5), (5, 6), (6, 2), (1, 5), (2, 6)]]
        gone = [[], [], [(7, 3), (7, 4)], [(7, 3), (7, 4), (3, 7), (4, 7), (0, 3)]]
    else:
        cracks = [[], [(4, 3), (3, 4), (3, 5), (2, 6)],
                  [(4, 3), (3, 4), (3, 5), (2, 6), (12, 11), (11, 12), (12, 12), (13, 4), (12, 3)],
                  [(4, 3), (3, 4), (3, 5), (2, 6), (12, 11), (11, 12), (12, 12), (13, 4), (12, 3),
                   (3, 11), (4, 12), (4, 13), (10, 2), (11, 2)]]
        gone = [[], [], [(15, 6), (15, 7), (14, 7), (15, 8)],
                [(15, 6), (15, 7), (14, 7), (15, 8), (6, 15), (7, 15), (7, 14), (8, 15), (0, 5), (0, 6), (1, 6)]]
    for (x, y) in cracks[dmg]:
        if c.get(x, y).startswith("concrete"):
            c.set(x, y, "outline")
    for (x, y) in gone[dmg]:
        c.set(x, y, "dirt")
    return c


# The 8-pixel pillbox: o outline, H lit concrete, c concrete, S shaded
# concrete, r the turret's ring, T the turret, "." the ground.
PILL8 = [".oooooo.",
         "oHHHHHco",
         "oHHrrcSo",
         "oHrTTrSo",
         "oHrTTrSo",
         "oHcrrSSo",
         "ocSSSSSo",
         ".oooooo."]
# Damage: cracks drawn in outline, then pieces knocked off.
PILL8_CRACKS = [[], [(5, 1), (6, 2)], [(5, 1), (6, 2), (1, 5), (2, 6)],
                [(5, 1), (6, 2), (1, 5), (2, 6), (2, 1), (6, 5)]]
PILL8_GONE = [[], [], [(7, 3), (7, 4)], [(7, 3), (7, 4), (3, 7), (4, 7), (0, 2), (1, 1)]]


def pill8(c, col, dmg):
    mat = {"o": "outline", "H": "concrete_hi", "c": "concrete", "S": "concrete_sh",
           "r": col[1], "T": col[0]}
    for y in range(8):
        for x in range(8):
            ch = PILL8[y][x]
            if ch != ".":
                c.set(x, y, mat[ch])
    for (x, y) in PILL8_CRACKS[dmg]:
        c.set(x, y, "outline")
    for (x, y) in PILL8_GONE[dmg]:
        c.set(x, y, "dirt")
    return c


def draw_base(s, owner):
    c = Cell(s, "concrete")
    col = {"OWN": "own", "ALLY": "ally", "ENEMY": "enemy", "NEUTRAL": "concrete_hi"}[owner]
    for y in range(s):
        for x in range(s):
            if x in (0, s - 1) or y in (0, s - 1):
                m = "outline"
            elif x == 1 or y == 1:
                m = "concrete_hi"
            elif x == s - 2 or y == s - 2:
                m = "concrete_sh"
            else:
                m = "concrete"
            c.set(x, y, m)
    if s == 16:
        for (x, y) in [(3, 3), (12, 3), (3, 12), (12, 12)]:
            c.set(x, y, "concrete_sh")
        lo, hi = 5, 10
    else:
        lo, hi = 2, 5
    for y in range(lo, hi + 1):
        for x in range(lo, hi + 1):
            edge = x in (lo, hi) or y in (lo, hi)
            c.set(x, y, "outline" if edge else col)
    if owner == "NEUTRAL" and s == 16:
        for y in range(lo + 2, hi - 1):
            for x in range(lo + 2, hi - 1):
                c.set(x, y, "concrete_sh")
    return c


def draw_start(s):
    c = Cell(s)
    for y in range(s):
        for x in range(s):
            c.set(x, y, water_at(s, x, y, deep=True))
    mid = s / 2
    r = s * 0.3
    sp = Cell(s)
    rasterise(sp, [("start", lambda x, y: abs(x - mid) + abs(y - mid) <= r)])
    outline(sp)
    for y in range(s):
        for x in range(s):
            if sp.get(x, y) != "clear":
                c.set(x, y, sp.get(x, y))
    return c


# ---------------------------------------------------------------------------
# Sprites. Heading d of 16 points up for 0 and turns clockwise.

def heading_angle(d):
    return d * math.pi / 8


# 8-pixel tanks drawn by hand for headings 0 (up), 1 and 2; the others are
# these turned and mirrored. t tread, H hull, S turret, b barrel. The
# outline is added round them.
TANK8 = [["...bb...",
          ".t.bb.t.",
          ".tHbbHt.",
          ".tHSSHt.",
          ".tHSSHt.",
          ".tHHHHt.",
          ".t....t.",
          "........"],
         ["..t..b..",
          "..t.bb..",
          ".tHHbH.t",
          ".tHSSHt.",
          ".tHSSHt.",
          "t.HHHHt.",
          ".....t..",
          ".....t.."],
         ["....t..b",
          "...tH.b.",
          "..tHHb..",
          ".tHSSHHt",
          "tHHSSHt.",
          "..HHHt..",
          "...Ht...",
          "...t...."]]


def grid_rot(g, q):
    """Turns a grid (list of strings) a quarter turn clockwise q times."""
    for _ in range(q % 4):
        n = len(g)
        g = ["".join(g[n - 1 - x][y] for x in range(n)) for y in range(n)]
    return g


def grid_mirror(g):
    """Mirrors a grid across the up-right diagonal: heading 1 becomes 3."""
    n = len(g)
    return ["".join(g[n - 1 - x][n - 1 - y] for x in range(n)) for y in range(n)]


def heading_grid(base, d):
    """The grid for heading d from grids for headings 0, 1 and 2."""
    r, q = d % 4, d // 4
    g = grid_mirror(base[1]) if r == 3 else base[r]
    return grid_rot(g, q)


def cell_to_grid(c, keymap):
    return ["".join(keymap.get(c.get(x, y), ".") for x in range(c.s)) for y in range(c.s)]


def tank_grids16(boat):
    """Headings 0, 1 and 2 at 16 pixels, drawn from shapes."""
    out = []
    for d in range(3):
        ang = heading_angle(d)
        scale = 0.8 if boat else 1.0
        treads = [[(-6.0, -6.0), (-3.0, -6.0), (-3.0, 6.0), (-6.0, 6.0)],
                  [(3.0, -6.0), (6.0, -6.0), (6.0, 6.0), (3.0, 6.0)]]
        hull = [(-3.0, -5.0), (3.0, -5.0), (3.0, 5.0), (-3.0, 5.0)]
        barrel = [(-1.0, -8.0), (1.0, -8.0), (1.0, 0), (-1.0, 0)]
        tr = [rotate(p, ang, 8, 8, scale) for p in treads]
        hl = rotate(hull, ang, 8, 8, scale)
        br = rotate(barrel, ang, 8, 8, scale)
        tcx, tcy = rotate([(0, 0.5)], ang, 8, 8, scale)[0]
        layers = [("tread", lambda x, y, p=p: poly_inside(p, x, y)) for p in tr]
        layers += [("tank_own", lambda x, y: poly_inside(hl, x, y)),
                   ("tank_own_sh", lambda x, y: math.hypot(x - tcx, y - tcy) <= 2.9 * scale),
                   ("barrel", lambda x, y: poly_inside(br, x, y))]
        c = Cell(16)
        rasterise(c, layers, thresh=0.45)
        out.append(cell_to_grid(c, {"tread": "t", "tank_own": "H", "tank_own_sh": "S", "barrel": "b"}))
    return out


def boat_grids(s):
    out = []
    for d in range(3):
        c = Cell(s)
        draw_boat_shape(c, heading_angle(d))
        out.append(cell_to_grid(c, {"boat": "B", "boat_hi": "D"}))
    return out


_GRIDS = {}


def draw_tank(s, owner, d, boat):
    hull, turret = {"OWN": ("tank_own", "tank_own_sh"), "ALLY": ("tank_ally", "tank_ally_sh"),
                    "ENEMY": ("tank_enemy", "tank_enemy_sh")}[owner]
    if (s, boat) not in _GRIDS:
        _GRIDS[s, boat] = (TANK8 if s == 8 else tank_grids16(boat), boat_grids(s))
    tanks, boats = _GRIDS[s, boat]
    mat = {"t": "tread", "H": hull, "S": turret, "b": "barrel", "B": "boat", "D": "boat_hi"}
    c = Cell(s)
    if boat:
        bg = heading_grid(boats, d)
        for y in range(s):
            for x in range(s):
                if bg[y][x] != ".":
                    c.set(x, y, mat[bg[y][x]])
    g = heading_grid(tanks, d)
    for y in range(s):
        for x in range(s):
            ch = g[y][x]
            # On a boat at 8 pixels there is no room for the treads.
            if ch == "." or (boat and s == 8 and ch == "t"):
                continue
            c.set(x, y, mat[ch])
    outline(c)
    return c


def draw_man(s, frame):
    c = Cell(s)
    if s == 8:
        px = [(3, 2, "man_skin"), (3, 3, "man_suit"), (2, 3, "man_suit"), (4, 3, "man_suit"),
              (3, 4, "man_suit")]
        legs = [[(2, 5), (4, 5)], [(3, 5), (3, 6)], [(2, 5), (4, 5)]][frame]
        if frame == 1:
            legs = [(3, 5), (4, 5)]
        elif frame == 2:
            legs = [(2, 5), (3, 5)]
    else:
        px = [(7, 3, "man_skin"), (8, 3, "man_skin"), (7, 4, "man_skin"), (8, 4, "man_skin")]
        px += [(x, y, "man_suit") for y in (6, 7, 8) for x in (6, 7, 8, 9)]
        px += [(x, 5, "man_suit") for x in (7, 8)]
        arms = [[(5, 6), (5, 7), (10, 6), (10, 7)], [(5, 6), (10, 7), (10, 8)], [(5, 7), (5, 8), (10, 6)]][frame]
        px += [(x, y, "man_suit") for (x, y) in arms]
        legs = [[(6, 9), (6, 10), (6, 11), (9, 9), (9, 10), (9, 11)],
                [(7, 9), (7, 10), (7, 11), (9, 9), (10, 10), (10, 11)],
                [(6, 9), (5, 10), (5, 11), (8, 9), (8, 10), (8, 11)]][frame]
    for (x, y, m) in px:
        c.set(x, y, m)
    for (x, y) in legs:
        c.set(x, y, "outline")
    outline(c)
    return c


def draw_para(s):
    c = Cell(s)
    mid = s / 2
    r = s * 0.45
    top = s * 0.5

    def canopy(x, y):
        return y < top and math.hypot(x - mid, (y - top) * 1.3) <= r

    def stripes(x, y):
        return "para_b" if int((x - mid + r) / (s / 4)) % 2 else "para_a"
    rasterise(c, [(stripes, canopy)])
    if s == 8:
        man = [(3, 6, "man_skin"), (4, 6, "man_skin"), (3, 7, "man_suit"), (4, 7, "man_suit"),
               (1, 4, "outline"), (6, 4, "outline"), (2, 5, "outline"), (5, 5, "outline")]
    else:
        man = [(7, 11, "man_skin"), (8, 11, "man_skin"), (7, 12, "man_suit"), (8, 12, "man_suit"),
               (6, 12, "man_suit"), (9, 12, "man_suit"), (7, 13, "man_suit"), (8, 13, "man_suit"),
               (7, 14, "outline"), (8, 14, "outline")]
        for i in range(3):
            man += [(3 + i, 8 + i, "outline"), (12 - i, 8 + i, "outline")]
    for (x, y, m) in man:
        c.set(x, y, m)
    outline(c)
    return c


def draw_shell(s, d):
    c = Cell(s)
    ang = heading_angle(d)
    fx, fy = math.sin(ang), -math.cos(ang)
    mid = s / 2
    half = s * (0.2 if s == 8 else 0.18)
    w = 0.5 if s == 8 else 0.8

    def core(x, y):
        a = (x - mid) * fx + (y - mid) * fy
        b = (x - mid) * (-fy) + (y - mid) * fx
        return abs(a) <= half and abs(b) <= w
    rasterise(c, [("shell", core)], thresh=0.3)
    if all(c.get(x, y) == "clear" for y in range(s) for x in range(s)):
        c.set(s // 2, s // 2, "shell")
    outline(c)
    return c


def draw_expl(s, k):
    c = Cell(s)
    mid = s / 2
    # Radius and colours by frame: a flash that grows, burns and turns
    # to smoke.
    rad = [0.2, 0.32, 0.42, 0.48, 0.5, 0.48, 0.42, 0.34][k] * s
    rings = [["expl_w"], ["expl_w", "expl_y"], ["expl_w", "expl_y", "expl_o"],
             ["expl_y", "expl_o", "expl_r"], ["expl_o", "expl_r", "smoke_d"],
             ["expl_r", "smoke", "smoke_d"], ["smoke", "smoke_d"], ["smoke"]][k]

    def mat(x, y):
        d = math.hypot(x - mid, y - mid) / rad
        i = min(len(rings) - 1, int(d * len(rings)))
        return rings[i]

    def shape(x, y):
        ang = math.atan2(y - mid, x - mid)
        wob = 1.0 + 0.18 * math.sin(ang * 5 + k) + 0.1 * math.sin(ang * 3 - k * 2)
        if k >= 6 and hash2(int(x), int(y), k) % 3 == 0:
            return False
        return math.hypot(x - mid, y - mid) <= rad * wob
    rasterise(c, [(mat, shape)])
    if k <= 4:
        outline(c, "outline") if k >= 3 else None
    return c


def draw_mine(s):
    c = Cell(s)
    mid = s / 2
    r = s * (0.22 if s == 8 else 0.2)
    rasterise(c, [("mine_body", lambda x, y: math.hypot(x - mid, y - mid) <= r + 0.3),
                  ("mine_hi", lambda x, y: math.hypot(x - mid + r * 0.35, y - mid + r * 0.35) <= r * 0.45)])
    if s == 8:
        c.set(3, 3, "mine_hi")
    else:
        for (x, y) in [(8, 4), (8, 11), (4, 8), (11, 8)]:
            c.set(x, y, "mine_body")
    outline(c)
    return c


# ---------------------------------------------------------------------------

def variant_cells(s, names, calc, alphabet, draw, prefer=None):
    """One cell per variant: the most common drawing among the
    neighbourhoods that give it."""
    looks = {nm: Counter() for nm in names}
    first = {}
    for n in combos(alphabet):
        v = calc(n)
        key = draw(s, n).key()
        looks[v][key] += 1
        first.setdefault((v, key), n)
    out = {}
    for nm in names:
        if prefer and nm in prefer:
            out[nm] = draw(s, prefer[nm])
            continue
        if not looks[nm]:
            raise SystemExit("no neighbourhood gives " + nm)
        key = looks[nm].most_common(1)[0][0]
        out[nm] = draw(s, first[(nm, key)])
    return out


def terrain_cells(s):
    cells = {"GRASS": draw_grass(s), "SWAMP": draw_swamp(s), "CRATER": draw_crater(s),
             "RUBBLE": draw_rubble(s), "HALFBUILDING": draw_half(s), "START": draw_start(s)}
    allroad = [ROAD] * 8
    # River on the diagonals makes no difference to a road.
    rd, rc = (ROAD, GRASS), (ROAD, RIVER, GRASS)
    cells.update(variant_cells(s, ROAD_NAMES, calc_road, [rd, rc, rd, rc, rc, rd, rc, rd], draw_road,
                               prefer={"ROAD_SOLID": allroad}))
    cells.update(variant_cells(s, BUILD_NAMES, calc_building, (BUILDING, GRASS), draw_building))
    cells.update(variant_cells(s, RIVER_NAMES, calc_river, (RIVER, GRASS), draw_river))
    cells.update(variant_cells(s, DEEP_NAMES, calc_deep, (DEEP, RIVER), draw_deep,
                               prefer={"DEEP_SOLID": [DEEP] * 8}))
    cells.update(variant_cells(s, FOREST_NAMES, calc_forest, (FOREST, GRASS), draw_forest,
                               prefer={"FOREST": [FOREST] * 8}))
    for nm in BOAT_NAMES:
        cells[nm] = draw_boat_tile(s, nm)
    for o in OWNERS:
        for d in range(4):
            cells["PILL_%s_%d" % (o, d)] = draw_pill(s, o, d)
    cells["PILL_DEAD"] = draw_pill(s, None, 0)
    for o in ("OWN", "ALLY", "ENEMY", "NEUTRAL"):
        cells["BASE_" + o] = draw_base(s, o)
    return [cells[n] for n in TERRAIN]


def icon_cells(s):
    return [draw_icon(s, n, False) for n in ICON_NAMES] + \
        [draw_icon(s, n, True) for n in ICON_NAMES] + [draw_icon(s, n, False) for n in ICON_EXTRA]


def sprite_cells(s):
    out = []
    for o in OWNERS:
        for boat in (False, True):
            for d in range(16):
                out.append(draw_tank(s, o, d, boat))
    out += [draw_man(s, f) for f in range(3)] + [draw_para(s)]
    out += [draw_shell(s, d) for d in range(16)]
    out += [draw_expl(s, k) for k in range(8)]
    out.append(draw_mine(s))
    out += icon_cells(s)
    assert len(out) == len(SPRITES)
    return out


def save_sheet(cells, s, mats, path):
    cols = 16
    rows = (len(cells) + cols - 1) // cols
    im = Image.new("P", (cols * s, rows * s), 0)
    pal = [0] * 768
    for name, (idx, rgb, _, _, _) in mats.items():
        pal[idx * 3:idx * 3 + 3] = list(rgb)
    im.putpalette(pal)
    px = im.load()
    for i, c in enumerate(cells):
        ox, oy = (i % cols) * s, (i // cols) * s
        for y in range(s):
            for x in range(s):
                px[ox + x, oy + y] = mats[c.get(x, y)][0]
    im.save(path, transparency=0)


def redraw_icons(out, mats):
    """Redraws the icon cells in the sprite sheets that are there, by their
    names in layout.txt, leaving the rest of the art as it is."""
    names = []
    for line in open(os.path.join(out, "layout.txt")):
        line = line.split("#")[0].strip()
        if line == "[sprites]":
            names = []
        elif line and not line.startswith("["):
            names.append(line)
    assert names[-len(SPRITES):] == SPRITES, "layout.txt sprites don't match SPRITES"
    pal = [0] * 768
    for name, (idx, rgb, _, _, _) in mats.items():
        pal[idx * 3:idx * 3 + 3] = list(rgb)
    for s in (8, 16):
        path = os.path.join(out, "sprites%d.png" % s)
        im = Image.open(path)
        im.putpalette(pal)
        px = im.load()
        for i, nm in enumerate(SPRITES):
            if not nm.startswith("ICON_"):
                continue
            base = nm[5:]
            sel = base.endswith("_SEL")
            c = draw_icon(s, base[:-4] if sel else base, sel)
            ox, oy = (i % 16) * s, (i // 16) * s
            for y in range(s):
                for x in range(s):
                    px[ox + x, oy + y] = mats[c.get(x, y)][0]
        im.save(path, transparency=0)


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else ART
    mats = load_palette(os.path.join(ART, "palette.txt"))
    if "--icons" in sys.argv:
        out = [a for a in sys.argv[1:] if a != "--icons"]
        redraw_icons(out[0] if out else ART, mats)
        return
    for s in (8, 16):
        save_sheet(terrain_cells(s), s, mats, os.path.join(out, "terrain%d.png" % s))
        save_sheet(sprite_cells(s), s, mats, os.path.join(out, "sprites%d.png" % s))
    with open(os.path.join(out, "layout.txt"), "w") as f:
        f.write("# Cell names of the art sheets, in order, 16 to a row.\n")
        f.write("[terrain]\n")
        f.write("".join(n + "\n" for n in TERRAIN))
        f.write("[sprites]\n")
        f.write("".join(n + "\n" for n in SPRITES))


if __name__ == "__main__":
    main()
