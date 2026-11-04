#!/usr/bin/env python3
# Decodes the PalmBoloLog database (a Palm .pdb) to text. Prints the lines
# and writes them to a .txt next to the .pdb.
# Usage: dumplog.py PalmBoloLog.pdb
import struct
import sys


def records(data):
    if len(data) < 78:
        raise SystemExit("too short for a Palm database")
    name = data[:32].split(b"\0")[0].decode("latin-1")
    dbtype, creator = struct.unpack(">4s4s", data[60:68])
    count, = struct.unpack(">H", data[76:78])
    offsets = []
    for i in range(count):
        off, = struct.unpack(">I", data[78 + 8 * i:82 + 8 * i])
        offsets.append(off)
    offsets.append(len(data))
    lines = []
    for i in range(count):
        rec = data[offsets[i]:offsets[i + 1]]
        lines.append(rec.split(b"\0")[0].decode("latin-1"))
    return name, dbtype, creator, lines


def main():
    if len(sys.argv) != 2:
        raise SystemExit(__doc__.strip().splitlines()[-1])
    path = sys.argv[1]
    with open(path, "rb") as f:
        name, dbtype, creator, lines = records(f.read())
    print("%s (%s/%s), %d records" % (name, dbtype.decode("latin-1"),
          creator.decode("latin-1"), len(lines)))
    text = "".join(line + "\n" for line in lines)
    sys.stdout.write(text)
    out = path[:-4] + ".txt" if path.lower().endswith(".pdb") else path + ".txt"
    with open(out, "w") as f:
        f.write(text)


main()
