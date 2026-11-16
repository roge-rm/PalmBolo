#!/usr/bin/env python3
"""Check compiled Sony bitmap families and code segment limits in a PRC."""
import struct
import sys
from pathlib import Path
from PIL import Image

raw = Path(sys.argv[1]).read_bytes()
count = struct.unpack_from('>H', raw, 76)[0]
entries = [struct.unpack_from('>4sHI', raw, 78 + 10*i) for i in range(count)]
resources = {}
for i, (kind, rid, offset) in enumerate(entries):
    end = entries[i+1][2] if i+1 < count else len(raw)
    resources[kind, rid] = raw[offset:end]
    if kind == b'code' and rid:
        assert end-offset < 32768, (rid, end-offset)
        print('code %d: %d bytes' % (rid, end-offset))
for rid, image, depths in ((3010,'terrain16-8.bmp',[1,2,4,8]),
                           (3011,'sprites16-8.bmp',[1,2,4,8]),
                           (3012,'masks16-1.bmp',[1]),
                           (3013,'title-320-8.bmp',[1,2,4,8])):
    data = resources[b'Tbmp',rid]
    assert len(data) < 65000, (rid,len(data))
    size = Image.open(Path('build/art') / image).size
    offset=0
    found=[]
    while True:
        w,h,row,flags,depth,version,next_depth=struct.unpack_from('>HHHHBBH',data,offset)
        assert (w,h)==size, (rid,w,h,size)
        assert version<=2, (rid,version)
        found.append(depth or 1)
        assert row >= (w*(depth or 1)+7)//8
        if not next_depth:
            break
        offset += 4*next_depth
        assert offset < len(data)
    assert found==depths, (rid,found)
    print('Sony bitmap %d: %dx%d, depths %s, %d bytes' % (rid,*size,found,len(data)))
print('Sony bitmap resources and code segments pass')
