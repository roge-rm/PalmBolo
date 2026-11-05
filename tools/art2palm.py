#!/usr/bin/env python3
# Turns the art sheets in res/art into what the Palm build needs:
#   OUT/*.bmp    one image per sheet and depth
#   OUT/art.rcp  the bitmap families for pilrc
#   OUT/art.h    cell numbers and resource IDs for the C code
#   OUT/*.rgb    the colour sheets raw, for host/maprender.c
# and with --preview DIR, PNGs of every sheet at every depth and of the
# icons alone, scaled up.
#
# Sheets (16 cells to a row):
#   terrain  8 pixels at 1, 2, 4 and 8 bits, 16 pixels at 8 bits
#            (double density)
#   sprites  the same, transparent where the material is "clear"
#   masks    1 bit, black where a sprite is drawn: one cell for each tank
#            heading on land and on a boat (shared by all owners), then one
#            for each other sprite
#
# Each pixel of a source PNG is a palette index, a row of palette.txt, which
# says what it is on each kind of screen.
#
# Usage: art2palm.py OUTDIR [--preview DIR]
import os
import struct
import sys

from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
ART = os.path.join(HERE, "..", "res", "art")
COLS = 16
RES_TERRAIN, RES_SPRITES, RES_MASKS, RES_TITLE = 3000, 3001, 3002, 3003
TANK_CELLS = 96         # 3 owners x land and boat x 16 headings


def load_palette():
    mats = {}
    for line in open(os.path.join(ART, "palette.txt")):
        f = line.split("#")[0].split()
        if f:
            mats[int(f[0])] = (f[1], (int(f[2]), int(f[3]), int(f[4])), int(f[5]), int(f[6]), f[7])
    return mats


def load_layout():
    sheets, cur = {}, None
    for line in open(os.path.join(ART, "layout.txt")):
        line = line.split("#")[0].strip()
        if not line:
            continue
        if line.startswith("["):
            cur = line.strip("[]")
            sheets[cur] = []
        else:
            sheets[cur].append(line)
    return sheets


def palm_palette():
    """The colours of Palm OS's 8-bit system palette."""
    cols = [(r, g, b) for r in range(0xFF, -1, -0x33) for b in range(0xFF, -1, -0x33)
            for g in range(0xFF, -1, -0x33)]
    cols += [(v, v, v) for v in (0x11, 0x22, 0x44, 0x55, 0x77, 0x88, 0xAA, 0xBB, 0xDD, 0xEE)]
    cols += [(0xC0, 0xC0, 0xC0), (0x80, 0, 0), (0x80, 0, 0x80), (0, 0x80, 0), (0, 0x80, 0x80)]
    return cols


PALM = palm_palette()


def nearest(rgb):
    return min(PALM, key=lambda c: (c[0] - rgb[0]) ** 2 + (c[1] - rgb[1]) ** 2 + (c[2] - rgb[2]) ** 2)


def mono_at(code, x, y):
    if code in "01":
        return int(code)
    if code == "c":
        return (x + y) & 1
    if code == "d":
        return 1 if (x % 2 == 0 and y % 2 == 0) else 0
    if code == "D":
        return 0 if (x % 2 == 0 and y % 2 == 0) else 1
    raise SystemExit("unknown 1-bit code " + code)


def render(src, mats, depth, clear_white=True):
    """src: indexed image. Returns an RGB image as that depth shows it (the
    colours are exact Palm levels)."""
    w, h = src.size
    out = Image.new("RGB", (w, h))
    sp, op = src.load(), out.load()
    for y in range(h):
        for x in range(w):
            i = sp[x, y]
            if i not in mats:
                raise SystemExit("palette index %d at %d,%d is not in palette.txt" % (i, x, y))
            name, rgb, g16, g4, mono = mats[i]
            if name == "clear" and clear_white:
                op[x, y] = (255, 255, 255)
            elif depth == 8:
                op[x, y] = nearest(rgb)
            elif depth == 4:
                v = 255 - g16 * 17
                op[x, y] = (v, v, v)
            elif depth == 2:
                v = 255 - g4 * 85
                op[x, y] = (v, v, v)
            else:
                v = 0 if mono_at(mono, x, y) else 255
                op[x, y] = (v, v, v)
    return out


def masks(src, s, n_sprites, clear):
    """The mask sheet for a sprite sheet with n_sprites cells."""
    sp = src.load()

    def cell_on(i, x, y):
        cx, cy = (i % COLS) * s, (i // COLS) * s
        return sp[cx + x, cy + y] != clear
    n = 32 + (n_sprites - TANK_CELLS)
    rows = (n + COLS - 1) // COLS
    out = Image.new("1", (COLS * s, rows * s), 1)
    op = out.load()
    for m in range(n):
        if m < 32:
            srcs = [o * 32 + m for o in range(3)]
        else:
            srcs = [m - 32 + TANK_CELLS]
        ox, oy = (m % COLS) * s, (m // COLS) * s
        for y in range(s):
            for x in range(s):
                if any(cell_on(i, x, y) for i in srcs):
                    op[ox + x, oy + y] = 0
    return out


def check_size(img, s, n, name):
    rows = (n + COLS - 1) // COLS
    if img.size != (COLS * s, rows * s):
        raise SystemExit("%s is %dx%d, expected %dx%d for %d cells" %
                         (name, img.size[0], img.size[1], COLS * s, rows * s, n))


def scaled(img, k):
    return img.resize((img.size[0] * k, img.size[1] * k), Image.NEAREST)


def icon_preview(src, mats, s, depth, names, path):
    """The ICON_ cells of a sprite sheet in a row as depth shows them, with
    a gap between, scaled up."""
    idx = [i for i, n in enumerate(names) if n.startswith("ICON_")]
    img = render(src, mats, depth)
    row = Image.new("RGB", (len(idx) * (s + 2) + 2, s + 4), (255, 255, 255))
    for k, i in enumerate(idx):
        x, y = (i % COLS) * s, (i // COLS) * s
        row.paste(img.crop((x, y, x + s, y + s)), (2 + k * (s + 2), 2))
    scaled(row, 64 // s).save(path)


def load_png(name):
    im = Image.open(os.path.join(ART, name + ".png"))
    if im.mode != "P":
        raise SystemExit("%s.png must be an indexed PNG" % name)
    return im


def family(out, members, with_ids=None):
    """pilrc lines for bitmap family members: (png name, depth, density,
    transparent, compress). The 8-bit icon members keep their clear pixels
    magenta for pilrc's TRANSPARENCY."""
    lines = []
    mats = load_palette()
    for name, depth, density, transparent, compress in members:
        img = render(load_png(name), mats, depth if depth != 4 else 4, clear_white=not transparent)
        f = "%s-%d.bmp" % (name, depth)
        (img.convert("1") if depth == 1 else img).save(os.path.join(out, f))
        lines.append('  BITMAP "%s" BPP %d%s%s%s\n' % (
            os.path.join(out, f), depth, " DENSITY 2" if density == 2 else "",
            " COMPRESS" if compress else "",
            " TRANSPARENCY 255 0 255" if transparent else ""))
    return lines


def title_and_icons(out, mats, preview):
    """The launcher icons (OS 3 before 3.5 reads the 32x22 1 and 2-bit
    members, later systems the 32x32 ones) and the title picture family.
    Only the title form loads the title, from storage."""
    r = []
    r.append("ICONFAMILYEX\nBEGIN\n")
    r += family(out, [("icon-22", 1, 1, False, False), ("icon-22", 2, 1, False, False),
                      ("icon-32", 4, 1, False, True), ("icon-32", 8, 1, True, True),
                      ("icon-64", 8, 2, True, True)])
    r.append("END\n\nSMALLICONFAMILYEX\nBEGIN\n")
    r += family(out, [("icon-15", 1, 1, False, False), ("icon-15", 2, 1, False, False),
                      ("icon-15", 4, 1, False, True), ("icon-15", 8, 1, True, True),
                      ("icon-30", 8, 2, True, True)])
    r.append("END\n\nBITMAP ID %d\nBEGIN\n" % RES_TITLE)
    r += family(out, [("title-160", 1, 1, False, False), ("title-160", 2, 1, False, False),
                      ("title-160", 4, 1, False, True), ("title-160", 8, 1, False, True),
                      ("title-320", 8, 2, False, True)])
    r.append("END\n\n")
    return r


def main():
    if len(sys.argv) < 2:
        raise SystemExit("usage: art2palm.py OUTDIR [--preview DIR]")
    out = os.path.abspath(sys.argv[1])
    preview = sys.argv[sys.argv.index("--preview") + 1] if "--preview" in sys.argv else None
    os.makedirs(out, exist_ok=True)
    mats = load_palette()
    clear = [i for i, m in mats.items() if m[0] == "clear"][0]
    layout = load_layout()
    nt, ns = len(layout["terrain"]), len(layout["sprites"])
    src = {}
    for sheet, n in (("terrain", nt), ("sprites", ns)):
        for s in (8, 16):
            im = Image.open(os.path.join(ART, "%s%d.png" % (sheet, s)))
            if im.mode != "P":
                raise SystemExit("%s%d.png must be an indexed PNG" % (sheet, s))
            check_size(im, s, n, "%s%d.png" % (sheet, s))
            src[sheet, s] = im
    rcp = ["// Made by tools/art2palm.py from res/art. Don't edit.\n"]
    for sheet, rid in (("terrain", RES_TERRAIN), ("sprites", RES_SPRITES)):
        rcp.append("BITMAP ID %d\nBEGIN\n" % rid)
        for depth in (1, 2, 4, 8):
            img = render(src[sheet, 8], mats, depth)
            f = "%s8-%d.bmp" % (sheet, depth)
            (img.convert("1") if depth == 1 else img).save(os.path.join(out, f))
            # OS 3.0 reads only uncompressed 1 and 2-bit members.
            rcp.append('  BITMAP "%s" BPP %d%s\n' % (os.path.join(out, f), depth,
                                                     " COMPRESS" if depth >= 4 else ""))
            if preview:
                scaled(img, 4).save(os.path.join(preview, "art-preview-%s8-%dbit.png" % (sheet, depth)))
        img = render(src[sheet, 16], mats, 8)
        for s in (8, 16):
            # Raw colour sheets for the host map renderer: width and height
            # as 16-bit little-endian numbers, then RGB.
            raw = render(src[sheet, s], mats, 8)
            with open(os.path.join(out, "%s%d.rgb" % (sheet, s)), "wb") as fh:
                fh.write(struct.pack("<HH", *raw.size) + raw.tobytes())
        f = "%s16-8.bmp" % sheet
        img.save(os.path.join(out, f))
        rcp.append('  BITMAP "%s" BPP 8 DENSITY 2 COMPRESS\n' % os.path.join(out, f))
        rcp.append("END\n\n")
        if preview:
            scaled(img, 2).save(os.path.join(preview, "art-preview-%s16-8bit.png" % sheet))
    if preview:
        for s, depths in ((8, (1, 2, 4, 8)), (16, (8,))):
            for depth in depths:
                icon_preview(src["sprites", s], mats, s, depth, layout["sprites"],
                             os.path.join(preview, "icons-preview-%d-%dbit.png" % (s, depth)))
    rcp.append("BITMAP ID %d\nBEGIN\n" % RES_MASKS)
    for s in (8, 16):
        m = masks(src["sprites", s], s, ns, clear)
        f = "masks%d-1.bmp" % s
        m.save(os.path.join(out, f))
        rcp.append('  BITMAP "%s" BPP 1%s\n' % (os.path.join(out, f), " DENSITY 2" if s == 16 else ""))
        if preview:
            scaled(m.convert("RGB"), 8 // (s // 8) * 1).save(
                os.path.join(preview, "art-preview-masks%d-1bit.png" % s))
    rcp.append("END\n")
    rcp.extend(title_and_icons(out, mats, preview))
    open(os.path.join(out, "art.rcp"), "w").write("".join(rcp))

    h = ["// Made by tools/art2palm.py from res/art/layout.txt. Don't edit.\n",
         "#ifndef ART_H\n#define ART_H\n\n",
         "#define ArtTerrainBmp %d\n#define ArtSpriteBmp %d\n#define ArtMaskBmp %d\n#define ArtTitleBmp %d\n" %
         (RES_TERRAIN, RES_SPRITES, RES_MASKS, RES_TITLE),
         "#define ART_COLS %d\n#define ART_TERRAIN_CELLS %d\n#define ART_SPRITE_CELLS %d\n" %
         (COLS, nt, ns),
         "#define ART_MASK_CELLS %d\n#define ART_TANK_CELLS %d\n\n" % (32 + ns - TANK_CELLS, TANK_CELLS)]
    for prefix, names in (("AT_", layout["terrain"]), ("AS_", layout["sprites"])):
        h.append("enum {\n")
        h.append("".join("\t%s%s = %d,\n" % (prefix, n, i) for i, n in enumerate(names)))
        h.append("};\n\n")
    h.append("#endif\n")
    open(os.path.join(out, "art.h"), "w").write("".join(h))


if __name__ == "__main__":
    main()
