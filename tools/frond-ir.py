#!/usr/bin/env python3
"""Run two PalmBolos over Frond's IR relay and keep frames and app logs.

Build a DEBUG PRC first. Two heavy emulator slots and two free IR ports are
needed. This is a diagnostic runner; inspect the logs for agreeing checksums.
It does not treat reaching the game screen as a passing network test.

Usage: frond-ir.py OUT [--host iiix|tx] [--join iiix|tx] [--speed original|uncapped]
       [--seconds 30] [--lobby-seconds 12] [--ports 5076 5077] [--prc FILE]
       [--core-68k FILE] [--iiix-session FILE] [--startup-timeout 3600]
The IIIx launcher session is the one made by frond-iiix.py. TX runs cold boot
from the calibrated images used by frond-m3.sh. Firmware copies are removed
when the run ends. Logs can fail to export when Palm OS stops servicing its
event loop; the console log keeps that failure.
"""
import argparse
import hashlib
import importlib.util
import json
import shutil
import subprocess
import sys
import time
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
FROND = Path.home() / 'Projects/palm/frond'
sys.path.insert(0, str(FROND / 'tools'))
import palm_boot  # noqa: E402


def load_iiix():
    spec = importlib.util.spec_from_file_location('iiix', ROOT / 'tools/frond-iiix.py')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('out', type=Path)
    parser.add_argument('--host', choices=['iiix', 'tx'], default='iiix')
    parser.add_argument('--join', choices=['iiix', 'tx'], default='iiix')
    parser.add_argument('--speed', choices=['original', 'uncapped'], default='original')
    parser.add_argument('--seconds', type=float, default=30)
    parser.add_argument('--lobby-seconds', type=float, default=12)
    parser.add_argument('--startup-timeout', type=float, default=3600,
                        help='seconds allowed for each IIIx to acquire a slot and start')
    parser.add_argument('--ports', type=int, nargs=2, default=[5076, 5077])
    parser.add_argument('--prc', type=Path, default=ROOT / 'build/PalmBolo.prc')
    parser.add_argument('--core-68k', type=Path, default=palm_boot.CORE_68K)
    parser.add_argument('--iiix-session', type=Path,
                        help='use this launcher session instead of the cached IIIx session')
    args = parser.parse_args()
    if args.ports[0] == args.ports[1]:
        parser.error('the IR ports must differ')
    for path in [args.prc, args.core_68k, args.iiix_session]:
        if path is not None and not path.is_file():
            parser.error(f'file does not exist: {path}')
    palm_boot.CORE_68K = args.core_68k.resolve()
    out = args.out.resolve()
    out.mkdir(parents=True, exist_ok=True)
    provenance = dict(host=args.host, join=args.join, speed=args.speed,
                      seconds=args.seconds, lobby_seconds=args.lobby_seconds)
    for name, path in [('core_68k', args.core_68k), ('prc', args.prc)]:
        provenance[name] = dict(path=str(path.resolve()),
                               sha256=hashlib.sha256(path.read_bytes()).hexdigest())
    commit = args.core_68k.resolve().parent / 'COMMIT'
    if commit.is_file():
        provenance['frond_commit'] = commit.read_text().strip()
    (out / 'provenance.json').write_text(json.dumps(provenance, indent=2) + '\n')
    cores, kinds = [], [args.host, args.join]
    relay = None
    relay_log = None
    try:
        for i, kind in enumerate(kinds):
            folder = out / str(i)
            folder.mkdir(exist_ok=True)
            print(f'Starting {kind} on IR port {args.ports[i]}, waiting for a heavy emulator slot.', flush=True)
            if kind == 'iiix':
                iiix = load_iiix()
                session = args.iiix_session or iiix.session_for(iiix.ROM)
                shutil.copyfile(session, folder / 'launcher.frond')
                boot = [folder / 'launcher.frond', '--device', 'palm-iiix', '--speed', args.speed]
            else:
                images = Path('/srv/downloads/temp/debug/palm/frond')
                shutil.copyfile(images / 'Palm-TX-5.4-nor.bin', folder / 'input-nor.bin')
                shutil.copyfile(images / 'tx-calibrated.bin', folder / 'input-nand.bin')
                boot = ['--device', 'palm-tx', folder / 'input-nor.bin', '--nand', folder / 'input-nand.bin']
            core = palm_boot.Core([*boot, '--irda', f'tcp:{args.ports[i]}'], folder / 'core.log')
            cores.append(core)
            if kind == 'iiix':
                core.send('speed-state', 'speed:', args.startup_timeout)
            else:
                palm_boot.tx_setup(core, folder)
            result = core.send(f'install "{args.prc.resolve()}"', ('installation successful', 'installation failed', 'failed to install'), 120)
            if 'successful' not in result:
                raise RuntimeError(result)
        relay_log = (out / 'relay.log').open('w')
        relay = subprocess.Popen([sys.executable, str(FROND / 'tools/link-irda.py'),
                                  *map(str, args.ports)], stdout=relay_log, stderr=subprocess.STDOUT)
        for i, core in enumerate(cores):
            core.launch('PalmBolo')
            core.frame(out / str(i) / 'title.png')

        def tap(i, low, high):
            cores[i].tap(out / str(i), *(low if kinds[i] == 'iiix' else high))
            time.sleep(1)

        tap(0, (40, 119), (160, 246))
        tap(0, (30, 150), (68, 428))
        tap(0, (80, 150), (160, 428))
        tap(1, (120, 119), (160, 286))
        tap(1, (30, 150), (68, 428))
        time.sleep(args.lobby_seconds)
        for i, core in enumerate(cores):
            core.frame(out / str(i) / 'lobby.png')
        tap(0, (30, 150), (68, 428))
        time.sleep(args.seconds)
        for i, core in enumerate(cores):
            folder = out / str(i)
            core.frame(folder / 'game.png')
            core.send('irda-stats', ('rx=', 'infrared'), 30)
            if kinds[i] == 'iiix':
                core.send(f'db-export-db PalmBoloLog "{folder / "PalmBoloLog.pdb"}"', ('export written', 'export failed'), 60)
            else:
                archive = folder / 'databases.zip'
                core.send(f'db-export "{archive}"', ('export written to', 'export FAILED', 'failed to initialize export', 'failed to write export'), 60)
                if archive.is_file():
                    with zipfile.ZipFile(archive) as data:
                        for name in data.namelist():
                            if Path(name).name == 'PalmBoloLog.pdb':
                                (folder / 'PalmBoloLog.pdb').write_bytes(data.read(name))
            log = folder / 'PalmBoloLog.pdb'
            if log.is_file():
                subprocess.run([sys.executable, str(ROOT / 'tools/dumplog.py'), str(log)], check=True,
                               stdout=subprocess.DEVNULL)
            else:
                print(f'{i}: no app log exported; see core.log', flush=True)
    finally:
        for core in cores:
            core.close()
        if relay:
            relay.terminate()
            relay.wait()
        if relay_log:
            relay_log.close()
        for i in range(2):
            for name in ['input-nand.bin', 'input-nor.bin', 'launcher.frond', 'core']:
                (out / str(i) / name).unlink(missing_ok=True)
            for replay in (out / str(i)).glob('console-*.replay'):
                replay.unlink()


if __name__ == '__main__':
    main()
