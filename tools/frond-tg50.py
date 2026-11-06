#!/usr/bin/env python3
"""Runs build/PalmBolo.prc on a Sony Clie TG50 in Frond (OS 5.0, 320x320).

Boots the NOR through setup once and keeps that launcher session in
build/tg50/ (it holds the firmware, so it stays out of the repo). Each run
goes through Frond's frond-run.py from the session: installs the PRC,
launches PalmBolo, plays the replay and exports PalmBoloLog (build with
EXTRA=-DAUTOQUIT_SECONDS=N so the app quits by itself). Other flags go to
frond-run.py.

TG50 keyboard keys in replays are numbers 17 to 58 (see KEYS below), the
same as Frond's key ids.

Usage: frond-tg50.py OUTDIR REPLAY [--nor NOR] [--prc PRC] [frond-run flags]
"""
import argparse
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent
FROND = Path.home() / 'Projects/palm/frond'
sys.path.insert(0, str(FROND / 'tools'))
import palm_boot  # noqa: E402

NOR = Path('/srv/downloads/backup/deviceimages/palm/Sony/CLIE-TG50/Sony-Clie-TG50-nor.bin')
DEVICE = 'sony-clie-tg50'


def tg50_key(drive, column):
    return 17 + (drive - 2) * 7 + column


# Frond's TG50 keyboard: key name to key id, for replays as kb:NAME,
# such as "100 key-down kb:space".
KEYS = {
    'q': (2, 1), 'w': (2, 2), 'e': (2, 3), 'r': (2, 4), 't': (2, 5),
    'y': (3, 1), 'u': (3, 2), 'i': (3, 3), 'o': (3, 4), 'p': (3, 5),
    'a': (4, 1), 's': (4, 2), 'd': (4, 3), 'f': (4, 4), 'g': (4, 5),
    'h': (2, 6), 'j': (3, 6), 'k': (4, 6), 'l': (5, 6),
    'tab': (3, 0), 'z': (5, 1), 'x': (5, 2), 'c': (5, 3), 'v': (5, 4),
    'b': (5, 5), 'n': (6, 1), 'm': (6, 2), 'backspace': (7, 6),
    'ctrl': (5, 0), 'blue': (6, 0), 'minus': (7, 2), 'comma': (6, 3),
    'stop': (6, 4), 'up': (6, 5), 'enter': (7, 5), 'shift': (4, 0),
    'red': (7, 0), 'space': (7, 1), 'left': (7, 3), 'down': (6, 6),
    'right': (7, 4), 'caps': (2, 0),
}


def session_for(nor):
    """The launcher session for a NOR, booting through setup the first time."""
    folder = ROOT / 'build/tg50'
    folder.mkdir(parents=True, exist_ok=True)
    # save-session writes a machine folder named after the file.
    session = folder / (nor.stem + '-launcher.frond')
    machine = folder / (nor.stem + '-launcher')
    if machine.is_dir():
        return machine
    core = palm_boot.Core(['--device', DEVICE, nor], folder / 'boot-log.txt')
    try:
        palm_boot.tg50_setup(core, folder)
        core.send(f'save-session "{session}"', 'wrote session', 60)
    finally:
        core.close()
        for f in folder.glob('console-*.replay'):
            f.unlink()
    return machine


def rewrite(text, out):
    """Replay lines with snapshot paths in OUTDIR and kb:NAME as key ids."""
    lines = []
    for line in text.splitlines():
        w = line.split()
        if len(w) >= 3 and w[1] == 'snapshot':
            line = f'{w[0]} snapshot "{out / w[2].strip(chr(34))}"'
        elif len(w) >= 3 and w[2].startswith('kb:'):
            line = f'{w[0]} {w[1]} {tg50_key(*KEYS[w[2][3:]])}'
        lines.append(line)
    return '\n'.join(lines) + '\n'


def main():
    p = argparse.ArgumentParser()
    p.add_argument('out', type=Path)
    p.add_argument('replay', type=Path)
    p.add_argument('--nor', type=Path, default=NOR)
    p.add_argument('--prc', type=Path, default=ROOT / 'build/PalmBolo.prc')
    a, rest = p.parse_known_args()
    out = a.out.resolve()
    session = session_for(a.nor)
    # frond-run.py wants a new output folder, so the replay sits beside it.
    replay = out.with_name(out.name + '.replay')
    replay.write_text(rewrite(a.replay.read_text(), Path('.')))
    try:
        r = subprocess.run([sys.executable, str(FROND / 'tools/frond-run.py'),
                            '--core', str(FROND / 'out/frond'), '--device', DEVICE,
                            '--session', str(session), '--install', str(a.prc.resolve()),
                            '--app', 'PalmBolo', '--replay', str(replay), '--headless',
                            '--output', str(out), '--export-db', 'PalmBoloLog', *rest])
    finally:
        replay.unlink(missing_ok=True)
    # The run folder's firmware and RAM copies aren't kept.
    for pat in ('input-*.bin', 'core', 'core.*', '*.frond', 'working'):
        for f in out.glob(pat):
            subprocess.run(['rm', '-rf', str(f)], check=False)
    sys.exit(r.returncode)


if __name__ == '__main__':
    main()
