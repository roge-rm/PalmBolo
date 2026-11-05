#!/usr/bin/env python3
# Draws the launcher icons and the title screen into res/art:
#   icon-32.png  32x32 large icon        icon-64.png  its double density
#   icon-22.png  32x22 large icon (OS 3 before 3.5)
#   icon-15.png  15x9 small icon         icon-30.png  its double density
#   title-160.png, title-320.png  title screens, the bottom 50 (100)
#                                 rows left plain for the buttons
# They are indexed PNGs on the materials of res/art/palette.txt, like the
# other art, so one source gives the 1-bit, grey and colour versions.
# Running this again overwrites hand edits to them.
#
# The art is seen from above like a Bolo tank: the hull is a Palm, the
# screen its hatch, the stylus its gun and treads down both sides.
#
# Usage: titleart.py [--preview DIR]
#   --preview writes a contact sheet, 4x, of every size at 1-bit, 4-grey
#   and 8-bit.
import os
import sys

from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import art2palm  # noqa: E402

ART = os.path.join(HERE, "..", "res", "art")
MATS = art2palm.load_palette()                       # index -> (name, ...)
IDX = {m[0]: i for i, m in MATS.items()}             # name -> index


# ---------------------------------------------------------------------------
# A canvas of material names.

class Canvas:
    def __init__(self, w, h, fill="clear"):
        self.w, self.h = w, h
        self.p = [[fill] * w for _ in range(h)]

    def get(self, x, y):
        return self.p[y][x] if 0 <= x < self.w and 0 <= y < self.h else "clear"

    def set(self, x, y, m):
        if 0 <= x < self.w and 0 <= y < self.h:
            self.p[y][x] = m

    def rect(self, x, y, w, h, m):
        for j in range(y, y + h):
            for i in range(x, x + w):
                self.set(i, j, m)

    def frame(self, x, y, w, h, m):
        self.rect(x, y, w, 1, m)
        self.rect(x, y + h - 1, w, 1, m)
        self.rect(x, y, 1, h, m)
        self.rect(x + w - 1, y, 1, h, m)

    def ellipse(self, x, y, w, h, m):
        cx, cy, rx, ry = x + w / 2, y + h / 2, w / 2, h / 2
        for j in range(y, y + h):
            for i in range(x, x + w):
                if ((i + 0.5 - cx) / rx) ** 2 + ((j + 0.5 - cy) / ry) ** 2 <= 1.0:
                    self.set(i, j, m)

    def poly(self, pts, m):
        x0, x1 = int(min(p[0] for p in pts)), int(max(p[0] for p in pts)) + 1
        y0, y1 = int(min(p[1] for p in pts)), int(max(p[1] for p in pts)) + 1
        for j in range(y0, y1):
            for i in range(x0, x1):
                if art2palm_inside(pts, i + 0.5, j + 0.5):
                    self.set(i, j, m)

    def grid(self, x, y, rows, keymap):
        for j, row in enumerate(rows):
            for i, ch in enumerate(row):
                if ch != ".":
                    self.set(x + i, y + j, keymap[ch])

    def paste(self, other, x, y):
        for j in range(other.h):
            for i in range(other.w):
                m = other.p[j][i]
                if m != "clear":
                    self.set(x + i, y + j, m)

    def outline(self, m="outline", diag=False):
        nb = [(-1, 0), (1, 0), (0, -1), (0, 1)]
        if diag:
            nb += [(-1, -1), (1, -1), (-1, 1), (1, 1)]
        add = [(x, y) for y in range(self.h) for x in range(self.w)
               if self.p[y][x] == "clear" and
               any(self.get(x + dx, y + dy) not in ("clear", m) for dx, dy in nb)]
        for (x, y) in add:
            self.set(x, y, m)

    def image(self):
        im = Image.new("P", (self.w, self.h), 0)
        pal = [0] * 768
        for i, m in MATS.items():
            pal[i * 3:i * 3 + 3] = list(m[1])
        im.putpalette(pal)
        px = im.load()
        for y in range(self.h):
            for x in range(self.w):
                px[x, y] = IDX[self.p[y][x]]
        return im


def art2palm_inside(pts, x, y):
    inside = False
    j = len(pts) - 1
    for i in range(len(pts)):
        xi, yi = pts[i]
        xj, yj = pts[j]
        if (yi > y) != (yj > y) and x < (xj - xi) * (y - yi) / (yj - yi) + xi:
            inside = not inside
        j = i
    return inside


class Units:
    """Draws on a canvas in design units of k pixels from (ox, oy). Lines
    (hl, vl) stay 1 pixel thick at any k; detail is on from k = 2."""

    def __init__(self, c, ox, oy, k):
        self.c, self.ox, self.oy, self.k = c, ox, oy, k
        self.detail = k >= 2

    def X(self, x):
        return self.ox + int(round(x * self.k))

    def Y(self, y):
        return self.oy + int(round(y * self.k))

    def r(self, x, y, w, h, m):
        self.c.rect(self.X(x), self.Y(y), self.X(x + w) - self.X(x), self.Y(y + h) - self.Y(y), m)

    def e(self, x, y, w, h, m):
        self.c.ellipse(self.X(x), self.Y(y), self.X(x + w) - self.X(x), self.Y(y + h) - self.Y(y), m)

    def poly(self, pts, m):
        self.c.poly([(self.ox + px * self.k, self.oy + py * self.k) for px, py in pts], m)

    def bevel(self, x, y, w, h, hi, sh):
        """1-pixel light top and left edges and dark bottom and right edges
        inside a unit rectangle, only with detail on."""
        if not self.detail:
            return
        X0, Y0, X1, Y1 = self.X(x), self.Y(y), self.X(x + w) - 1, self.Y(y + h) - 1
        if hi:
            self.c.rect(X0, Y0, X1 - X0, 1, hi)
            self.c.rect(X0, Y0, 1, Y1 - Y0, hi)
        if sh:
            self.c.rect(X0 + 1, Y1, X1 - X0, 1, sh)
            self.c.rect(X1, Y0 + 1, 1, Y1 - Y0, sh)


# ---------------------------------------------------------------------------
# Game art: cells of the existing sheets, by name.

_SHEETS = {}


def cell(sheet, name, s):
    """A cell of terrain or sprites at 8 or 16 pixels as a Canvas."""
    key = (sheet, s)
    if key not in _SHEETS:
        _SHEETS[key] = Image.open(os.path.join(ART, "%s%d.png" % (sheet, s))).load()
    names = art2palm.load_layout()[sheet]
    i = names.index(name)
    px = _SHEETS[key]
    c = Canvas(s, s)
    ox, oy = (i % 16) * s, (i // 16) * s
    for y in range(s):
        for x in range(s):
            c.p[y][x] = MATS[px[ox + x, oy + y]][0]
    return c


# ---------------------------------------------------------------------------
# A Bolo tank from above with a Palm for a hull.
# Design grid 32 wide; h is 32 or 22.

def tread(u, x, y, w, h, step=2):
    u.r(x, y, w, h, "tread")
    yy = y + 1
    while yy < y + h - 1:
        u.r(x + 1 if w > 3 else x, yy, w - 2 if w > 3 else w, 0.5 if u.detail else 1, "tread_hi")
        yy += step


def stylus_v(u, x, y0, y1):
    """A stylus pointing up from y1 to its tip at y0, 2 units wide."""
    u.r(x, y0, 2, y1 - y0, "stylus")
    if u.detail:
        u.c.rect(u.X(x), u.Y(y0), 1, u.Y(y1) - u.Y(y0), "stylus_hi")
        u.c.rect(u.X(x + 2) - 1, u.Y(y0), 1, u.Y(y1) - u.Y(y0), "pda_case_sh")
        u.r(x, y1 - 1.5, 2, 0.5, "pda_case_sh")
    u.r(x, y0, 2, 1.5 if u.detail else 2, "stylus_tip")


def palm_buttons_h(u, x, y, w):
    """The Palm's 4 round buttons in a row with a d-pad in the middle. At 1
    pixel to the unit there's no room for a plus between the buttons, so
    the d-pad is a bar twice their height."""
    k = u.k
    mx = x + w / 2
    if not u.detail:
        for bx in (x, x + 3, x + w - 5, x + w - 2):
            u.r(bx, y, 2, 2, "pda_btn")
        u.r(mx - 1, y - 1, 2, 4, "pda_btn")
        return
    for bx in (x, x + 3, x + w - 5, x + w - 2):
        u.e(bx, y, 2, 2, "pda_btn")
        u.c.set(u.X(bx) + k // 2, u.Y(y) + k // 2, "pda_btn_hi")
    # D-pad: a plus 3 units across with arms 1 unit wide, centred on the
    # buttons' row.
    cy = y + 1
    u.r(mx - 1.5, cy - 0.5, 3, 1, "pda_btn")
    u.r(mx - 0.5, cy - 1.5, 1, 3, "pda_btn")
    u.r(mx - 0.25, cy - 0.25, 0.5, 0.5, "pda_btn_hi")
    u.c.set(u.X(mx - 0.5), u.Y(cy - 1.5), "pda_btn_hi")


def screen_tank(u, x, y, w, h, game_s=None):
    """What a Palm screen shows: the Bolo tank, as a glyph or the game's own
    sprite on grass."""
    if game_s:
        g = Canvas(u.X(x + w) - u.X(x), u.Y(y + h) - u.Y(y))
        for j in range(0, g.h, game_s):
            for i in range(0, g.w, game_s):
                g.paste(cell("terrain", "GRASS", game_s), i, j)
        if g.w >= 3 * game_s:
            g.paste(cell("terrain", "PILL_ENEMY_0", game_s), g.w - game_s, 0)
            g.paste(cell("terrain", "FOREST_SINGLE", game_s), 0, g.h - game_s)
        t = cell("sprites", "TANK_OWN_0", game_s)
        g.paste(t, (g.w - game_s) // 2, (g.h - game_s + 1) // 2)
        u.c.paste(g, u.X(x), u.Y(y))
        return
    u.r(x, y, w, h, "lcd")
    glyph = ["..#..",
             "..#..",
             "#####",
             "##.##",
             "#####",
             "#...#"]
    if h < 6:
        glyph = ["..#..", "#.#.#", "#####", "#...#"]
    elif h < 7:
        glyph = glyph[1:]
    k = u.k
    gx = u.X(x) + (u.X(x + w) - u.X(x) - 5 * k) // 2
    gy = u.Y(y) + (u.Y(y + h) - u.Y(y) - len(glyph) * k) // 2
    for j, row in enumerate(glyph):
        for i, ch in enumerate(row):
            if ch == "#":
                u.c.rect(gx + i * k, gy + j * k, k, k, "lcd_px")


def draw_a(c, ox, oy, k, h=32, game_s=None):
    u = Units(c, ox, oy, k)
    if h == 32:
        bar0, top, bot, scr = 1, 8, 30, (10, 11, 12, 11)
    else:
        bar0, top, bot, scr = 1, 6, 21, (10, 8, 12, 5)
    tread(u, 3, top, 5, bot - top)
    tread(u, 24, top, 5, bot - top)
    stylus_v(u, 15, bar0, top + 1)
    u.r(8, top + 1, 16, bot - top - 2, "pda_case")
    u.bevel(8, top + 1, 16, bot - top - 2, "pda_case_hi", "pda_case_sh")
    sx, sy, sw, sh = scr
    u.r(sx - 1, sy - 1, sw + 2, sh + 2, "pda_bezel")
    if game_s:
        screen_tank(u, sx, sy, sw, sh, game_s)
    else:
        screen_tank(u, sx, sy, sw, sh)
    by = sy + sh + 3
    palm_buttons_h(u, 9, by, 14)
    if u.detail:
        # Mounting bolts where the Palm sits on the treads.
        for yy in (top + 2, bot - 3):
            u.r(7.5, yy, 1, 1, "pda_case_sh")
            u.r(23.5, yy, 1, 1, "pda_case_sh")


# ---------------------------------------------------------------------------
# Icons.

def icon(w, h):
    """The large icons: drawn in a 32 grid at 1 or 2 pixels to the unit and
    outlined."""
    k = w // 32
    c = Canvas(w, h)
    draw_a(c, 0, 0, k, 32 if h == w else 22, game_s=16 if k == 2 else None)
    c.outline()
    return c


# 15x9, pointing right.
SMALL = ["##########.....",
         "#.#.#.#.##.....",
         "##########.....",
         "#...####.#.....",
         "#...#ll#.######",
         "#...####.#.....",
         "##########.....",
         "#.#.#.#.##.....",
         "##########....."]

SMALL_KEYS = {"#": "outline", "c": "pda_case", "l": "lcd", "t": "tread", "y": "stylus"}


def small_icon(k):
    c = Canvas(15 * k, 9 * k)
    rows = SMALL
    if k == 1:
        c.grid(0, 0, rows, SMALL_KEYS)
        return c
    # Double density: the same picture with each pixel doubled, then the
    # screens get LCD shading.
    for y, row in enumerate(rows):
        for x, ch in enumerate(row):
            if ch != ".":
                c.rect(x * 2, y * 2, 2, 2, SMALL_KEYS[ch])
    return c


# ---------------------------------------------------------------------------
# Title screens.

FONT = {
    "P": ["#####.", "######", "##..##", "##..##", "######", "#####.", "##....", "##....", "##...."],
    "B": ["#####.", "######", "##..##", "##.##.", "#####.", "##..##", "##..##", "######", "#####."],
    "a": ["", "", "", "#####.", "....##", ".#####", "##..##", "##..##", ".#####"],
    "l": ["##"] * 9,
    "m": ["", "", "", "#########.", "##########", "##..##..##", "##..##..##", "##..##..##", "##..##..##"],
    "o": ["", "", "", ".####.", "######", "##..##", "##..##", "######", ".####."],
}


def logo(k):
    """The word PalmBolo at k pixels to the unit, filled, outlined and with
    an extruded shadow."""
    word = "PalmBolo"
    wd = sum(len(FONT[ch][0] or FONT[ch][-1]) for ch in word) + len(word) - 1
    ex = max(1, k)       # extrusion depth in pixels
    c = Canvas(wd * k + 2 + ex, 9 * k + 2 + ex)
    fill = Canvas(c.w, c.h)
    x = 0
    for ch in word:
        g = FONT[ch]
        gw = len(g[-1])
        for j, row in enumerate(g):
            for i, b in enumerate(row):
                if b == "#":
                    fill.rect(1 + (x + i) * k, 1 + j * k, k, k, "logo")
        x += gw + 1
    # Shades by row: light at the top, darker at the bottom.
    for y in range(fill.h):
        for xx in range(fill.w):
            if fill.p[y][xx] == "logo":
                v = (y - 1) / (9 * k)
                if v < 0.3:
                    fill.p[y][xx] = "logo_hi"
                elif v > 0.72:
                    fill.p[y][xx] = "logo_sh"
    for d in range(ex, 0, -1):
        for y in range(fill.h):
            for xx in range(fill.w):
                if fill.p[y][xx] != "clear":
                    c.set(xx + d, y + d, "logo_ex")
    c.paste(fill, 0, 0)
    c.outline()
    return c


# Where things stand on the title's map, in tiles of the game (8 pixels at
# 160, 16 at 320): (column, row, sheet, cell).
TITLE_MAP = [(1, 5, "terrain", "FOREST_SINGLE"), (2, 9, "terrain", "FOREST_SINGLE"),
             (16, 5, "terrain", "PILL_ENEMY_0"), (18, 9, "terrain", "FOREST_SINGLE"),
             (16, 12, "terrain", "FOREST_SINGLE"), (1, 12, "terrain", "BASE_NEUTRAL"),
             (3, 7, "terrain", "BUILD_SINGLE"), (17, 8, "terrain", "CRATER"),
             (15.5, 4.5, "sprites", "EXPL_2"), (13, 7.5, "sprites", "SHELL_2")]

# The tank on the title: units to a pixel at 160, and x, y of it at 160.
TITLE_ART = (2, 48, 38)
TITLE_ROAD = 11     # the row of the road across the title, in tiles
PLAIN = 50          # rows at the bottom left plain at 160


def title(size):
    z = size // 160                     # 1 at 160, 2 at 320
    ts = 8 * z                          # game tile size
    c = Canvas(size, size, "white")
    top = size - PLAIN * z
    rows = (top + ts - 1) // ts
    ground = Canvas(size, rows * ts)
    grass = cell("terrain", "GRASS", ts)
    for j in range(rows):
        for i in range(size // ts):
            ground.paste(grass, i * ts, j * ts)
    for i in range(size // ts):
        ground.paste(cell("terrain", "ROAD_HORZ", ts), i * ts, TITLE_ROAD * ts)
    for (tx, ty, sheet, name) in TITLE_MAP:
        ground.paste(cell(sheet, name, ts), int(tx * ts), int(ty * ts))
    # The concept, larger and with detail, with a shadow on the ground.
    k, ax, ay = TITLE_ART
    art = Canvas(size, size)
    draw_a(art, ax * z, ay * z, k * z, 32, game_s=8 * z)
    art.outline()
    off = 3 * z
    for y in range(art.h):
        for x in range(art.w):
            if art.p[y][x] != "clear" and 0 <= y + off < ground.h and x + off < size:
                ground.set(x + off, y + off, "ground_sh")
    ground.paste(art, 0, 0)
    # Logo at the top.
    # Flecks behind it stay off on 1-bit screens so it reads.
    lg = logo(2 * z)
    lx, ly = (size - lg.w) // 2, 6 * z
    for y in range(ly - 3 * z, ly + lg.h + 3 * z):
        for x in range(lx - 4 * z, lx + lg.w + 4 * z):
            if ground.get(x, y) == "grass_f":
                ground.set(x, y, "grass_ft")
    ground.paste(lg, lx, ly)
    for y in range(top):
        c.p[y] = ground.p[y][:]
    c.rect(0, top, size, z, "outline")
    return c


# ---------------------------------------------------------------------------
# Output.

def sources():
    """name -> Canvas."""
    return {
        "icon-15": small_icon(1),
        "icon-30": small_icon(2),
        "icon-22": icon(32, 22),
        "icon-32": icon(32, 32),
        "icon-64": icon(64, 64),
        "title-160": title(160),
        "title-320": title(320),
    }


DEPTHS = ((1, "1-bit"), (2, "4-grey"), (8, "8-bit colour"))


def rendered(im, depth):
    return art2palm.render(im, MATS, depth)


def label(img, text):
    from PIL import ImageDraw
    ImageDraw.Draw(img).text((4, 2), text, fill=(0, 0, 0))


def contact(imgs, path, scale=4):
    """Every size and depth, scaled up: icons in a block on the left, titles
    beside them."""
    order = ["icon-15", "icon-30", "icon-22", "icon-32", "icon-64", "title-160", "title-320"]
    gap = 16
    cols_w = [imgs[n].size[0] * scale for n in order]
    row_h = max(imgs[n].size[1] for n in order) * scale
    W = sum(cols_w) + gap * (len(order) + 1)
    H = (row_h + gap + 14) * len(DEPTHS) + gap + 14
    sheet = Image.new("RGB", (W, H), (200, 200, 200))
    label(sheet, "%dx. Columns: %s" % (scale, ", ".join("%dx%d" % imgs[n].size for n in order)))
    y = gap + 14
    for depth, dname in DEPTHS:
        label_img = Image.new("RGB", (W, 14), (200, 200, 200))
        label(label_img, dname)
        sheet.paste(label_img, (0, y - 14))
        x = gap
        for n, cw in zip(order, cols_w):
            r = art2palm.scaled(rendered(imgs[n], depth), scale)
            sheet.paste(r, (x, y))
            x += cw + gap
        y += row_h + gap + 14
    sheet.save(path)


def main():
    args = sys.argv[1:]
    preview = None
    if "--preview" in args:
        i = args.index("--preview")
        preview = args[i + 1]
        del args[i:i + 2]
    imgs = {}
    for name, c in sources().items():
        im = c.image()
        im.save(os.path.join(ART, name + ".png"), transparency=0)
        imgs[name] = im
    if preview:
        os.makedirs(preview, exist_ok=True)
        contact(imgs, os.path.join(preview, "title-and-icons.png"))


if __name__ == "__main__":
    main()
