#!/usr/bin/env python3
# Wraps WinBolo .map files into Palm databases (type "Map ", creator "Rmrf")
# that PalmBolo lists in its map list. The database holds the raw .map bytes
# in records of 8000 bytes or less.
#
# Usage: map2pdb.py [-o OUTDIR] FILE.map...
# The database name is the file name without .map, 31 characters at most. A
# longer name is cut and ends in a 4-digit hex tag of the full name, and names
# that come out the same in one run get " 2", " 3" and so on.
import argparse
import os
import struct
import sys
import time
import zlib

RECORD_BYTES = 8000
PALM_EPOCH_OFFSET = 2082844800  # seconds from 1904 to 1970


def db_name(base, used):
    name = base
    if len(name.encode("latin-1", "replace")) > 31:
        tag = "%04x" % (zlib.crc32(base.encode("utf-8")) & 0xFFFF)
        name = name[:26].rstrip() + " " + tag
    n = 2
    unique = name
    while unique in used:
        suffix = " %d" % n
        unique = name[:31 - len(suffix)] + suffix
        n += 1
    used.add(unique)
    return unique


def make_pdb(name, data):
    chunks = [data[i:i + RECORD_BYTES] for i in range(0, len(data), RECORD_BYTES)] or [b""]
    now = int(time.time()) + PALM_EPOCH_OFFSET
    head = name.encode("latin-1", "replace").ljust(32, b"\0")
    head += struct.pack(">HHIIIIII4s4sIIH", 0, 1, now, now, 0, 0, 0, 0,
                        b"Map ", b"Rmrf", 0, 0, len(chunks))
    assert len(head) == 78
    offset = 78 + 8 * len(chunks) + 2
    table = b""
    for i, c in enumerate(chunks):
        table += struct.pack(">IB3s", offset, 0, bytes([0, 0, i + 1]))
        offset += len(c)
    return head + table + b"\0\0" + b"".join(chunks)


def main():
    ap = argparse.ArgumentParser(description="Wraps .map files into Palm databases for PalmBolo.")
    ap.add_argument("files", nargs="+")
    ap.add_argument("-o", "--outdir", default=".")
    args = ap.parse_args()
    os.makedirs(args.outdir, exist_ok=True)
    used = set()
    for path in args.files:
        with open(path, "rb") as f:
            data = f.read()
        if data[:8] != b"BMAPBOLO":
            print("%s: not a BMAPBOLO map file" % path, file=sys.stderr)
            return 1
        base = os.path.splitext(os.path.basename(path))[0]
        name = db_name(base, used)
        out = os.path.join(args.outdir, name.replace("/", "_") + ".pdb")
        with open(out, "wb") as f:
            f.write(make_pdb(name, data))
        print("%s -> %s (%s, %d bytes)" % (path, out, name, len(data)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
