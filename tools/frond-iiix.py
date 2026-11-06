#!/usr/bin/env python3
"""Runs build/PalmBolo.prc on a Palm IIIx in Frond (frond-68k, OS 3.1).

Boots the ROM through setup once and keeps that launcher session in
build/iiix/ (it holds the ROM, so it stays out of the repo). Each run starts
from the session, installs the PRC, launches PalmBolo, plays the replay
(timed in guest ms from the launch), and with --log waits for PalmBoloLog to
hold a "stop tick" line (build with EXTRA=-DAUTOQUIT_SECONDS=N) and writes it
as OUTDIR/PalmBoloLog.pdb and .txt. --seconds runs that long instead.

Usage: frond-iiix.py OUTDIR [--replay R] [--log] [--seconds N]
       [--timeout N] [--speed original|uncapped] [--rom ROM] [--prc PRC]
--speed original (the default) is the IIIx's 16.58 MHz; uncapped runs the
CPU 8 times faster with timers on real time, fine for checksums, not times.
"""
import argparse
import subprocess
import sys
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent
FROND = Path.home() / 'Projects/palm/frond'
sys.path.insert(0, str(FROND / 'tools'))
import palm_boot  # noqa: E402

ROM = Path('/srv/downloads/backup/deviceimages/palm/Palm/IIIx/Palm-IIIx-3.1-en.rom')


def session_for(rom):
    """The launcher session for a ROM, booting through setup the first time."""
    folder = ROOT / 'build/iiix'
    folder.mkdir(parents=True, exist_ok=True)
    session = folder / (rom.stem + '-launcher.frond')
    if session.is_file():
        return session
    core = palm_boot.Core(['--rom', rom, '--device', 'palm-iiix'], folder / 'boot-log.txt')
    try:
        palm_boot.iiix_setup(core, folder)
        core.send(f'save-session "{session}"', 'wrote session', 60)
    finally:
        core.close()
    return session


def main():
    p = argparse.ArgumentParser()
    p.add_argument('out', type=Path)
    p.add_argument('--replay', type=Path)
    p.add_argument('--log', action='store_true')
    p.add_argument('--seconds', type=float, default=0)
    p.add_argument('--timeout', type=float, default=1800)
    p.add_argument('--speed', default='original', choices=['original', 'uncapped'])
    p.add_argument('--rom', type=Path, default=ROM)
    p.add_argument('--prc', type=Path, default=ROOT / 'build/PalmBolo.prc')
    a = p.parse_args()
    out = a.out.resolve()
    out.mkdir(parents=True, exist_ok=True)
    session = session_for(a.rom)
    core = palm_boot.Core([session, '--device', 'palm-iiix', '--speed', a.speed], out / 'core.log')
    try:
        core.until('speed-state', 'speed:', 'speed', 30)
        reply = core.send(f'install "{a.prc.resolve()}"', 'install', 60)
        if 'successful' not in reply:
            raise SystemExit(f'install: {reply.strip()}')
        if a.replay:
            # Snapshot paths in the replay are relative to OUTDIR.
            lines = []
            for line in a.replay.read_text().splitlines():
                w = line.split()
                if len(w) >= 3 and w[1] == 'snapshot':
                    name = w[2].strip('"')
                    line = f'{w[0]} snapshot "{out / name}"'
                lines.append(line)
            (out / 'input.replay').write_text('\n'.join(lines) + '\n')
        core.launch('PalmBolo')
        if a.replay:
            core.send(f'input-replay "{out / "input.replay"}"', 'input replay loaded')
        start = time.monotonic()
        if a.seconds:
            time.sleep(a.seconds)
        if a.log:
            pdb = out / 'PalmBoloLog.pdb'
            while time.monotonic() - start < a.timeout:
                time.sleep(10)
                pdb.unlink(missing_ok=True)
                core.send(f'db-export-db PalmBoloLog "{pdb}"', 'export', 60)
                if pdb.is_file() and b'stop tick' in pdb.read_bytes():
                    break
            else:
                print('no "stop tick" in the log before the timeout')
            if pdb.is_file():
                subprocess.run([sys.executable, str(HERE / 'dumplog.py'), str(pdb)],
                               stdout=subprocess.DEVNULL, check=False)
        core.send('speed-state', 'speed:')
        core.frame(out / 'end.png')
    finally:
        core.close()
        (out / 'input.replay').unlink(missing_ok=True)
        for f in out.glob('console-*.replay'):
            f.unlink()


if __name__ == '__main__':
    main()
