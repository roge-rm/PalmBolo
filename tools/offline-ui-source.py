#!/usr/bin/env python3
"""Copy the production transition handlers into the focused host UI harness."""
from pathlib import Path
import re
source = Path('src/main.c').read_text()
out = []
for name in ('OfflineGame', 'LeaveGame', 'NetTurns', 'AppStop'):
    match = re.search(r'static (?:Boolean|void) SEC_COLD ' + name + r'\([^)]*\)\n\{', source)
    assert match, name
    start = match.start()
    pos = match.end()
    depth = 1
    while depth:
        depth += (source[pos] == '{') - (source[pos] == '}')
        pos += 1
    out.append(source[start:pos])
Path('build/offline-ui.inc').write_text('\n\n'.join(out) + '\n')
