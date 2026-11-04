#!/usr/bin/env python3
# Lists the resources in a PRC.
# Usage: prcinfo.py PalmBolo.prc
import struct
import sys

d = open(sys.argv[1], "rb").read()
name = d[:32].split(b"\0")[0].decode("latin-1")
typ, cr = struct.unpack(">4s4s", d[60:68])
n, = struct.unpack(">H", d[76:78])
print(name, typ.decode(), cr.decode(), n, "resources")
offs = [struct.unpack(">4sHI", d[78 + 10 * i:88 + 10 * i]) for i in range(n)]
for i, (t, rid, off) in enumerate(offs):
    end = offs[i + 1][2] if i + 1 < n else len(d)
    print(" ", t.decode("latin-1"), rid, end - off)
