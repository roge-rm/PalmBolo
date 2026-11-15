#!/usr/bin/env python3
"""Run two PalmBolos over Frond's IR relay and keep frames and app logs.

Build a DEBUG PRC first. Two heavy emulator slots and two free IR ports are
needed on the build host. The runner asserts checksum progress and the
requested outcome, retaining result.json, logs, screenshots and provenance.

Usage: frond-ir.py OUT [--host iiix|tx] [--join iiix|tx] [--speed original|uncapped]
       [--seconds 30] [--lobby-seconds 12] [--ports 5076 5077] [--prc FILE]
       [--core-68k FILE] [--iiix-session FILE] [--startup-timeout 3600]
The IIIx launcher session is the one made by frond-iiix.py. Prefer a TX
launcher calibrated with the same Frond revision via --tx-session. The cold
boot fallback uses the images from frond-m3.sh. Firmware copies are removed
when the run ends. Logs can fail to export when Palm OS stops servicing its
event loop; the console log keeps that failure.
"""
import argparse
import hashlib
import importlib.util
import json
import os
import platform
import re
import struct
import shutil
import subprocess
import sys
import time
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
FROND = Path(os.environ.get('PALMBOLO_FROND_ROOT', Path.home() / 'Projects/palm/frond'))
sys.path.insert(0, str(FROND / 'tools'))
import palm_boot  # noqa: E402


def load_iiix():
    spec = importlib.util.spec_from_file_location('iiix', ROOT / 'tools/frond-iiix.py')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def pdb_payload(path):
    data = path.read_bytes()
    count = struct.unpack_from('>H', data, 76)[0]
    offsets = [struct.unpack_from('>I', data, 78 + 8 * i)[0] for i in range(count)] + [len(data)]
    return b''.join(data[offsets[i]:offsets[i + 1]] for i in range(count))


def export_log(core, kind, folder, tx_finished=False):
    log = folder / 'PalmBoloLog.pdb'
    if kind == 'iiix':
        core.send(f'db-export-db PalmBoloLog "{log}"', ('export written', 'export failed'), 60)
    else:
        if not tx_finished:
            raise RuntimeError('refusing full TX database export while IR may be live')
        archive = folder / 'databases.zip'
        core.send(f'db-export "{archive}"', ('export written to', 'export FAILED', 'failed to initialize export', 'failed to write export'), 60)
        if archive.is_file():
            with zipfile.ZipFile(archive) as data:
                for name in data.namelist():
                    if Path(name).name == 'PalmBoloLog.pdb':
                        log.write_bytes(data.read(name))
    return log


def wait_play(core, kind, folder, timeout=120):
    end = time.monotonic() + timeout
    while time.monotonic() < end:
        log = export_log(core, kind, folder)
        if log.is_file() and b'start: view open' in log.read_bytes():
            (folder / 'ready-log.pdb').write_bytes(log.read_bytes())
            core.frame(folder / 'ready.png')
            return
        time.sleep(2)
    raise RuntimeError('game view did not open before input replay')


def leave_on_wire(path, side):
    buffers, escaped = [None, None], [False, False]
    for line in path.read_text().splitlines():
        try:
            record = json.loads(line)
        except ValueError:
            continue
        if 'hex' not in record:
            continue
        source = record['source']
        for byte in bytes.fromhex(record['hex']):
            if byte == 0xc0:
                buffers[source], escaped[source] = bytearray(), False
            elif byte == 0xc1:
                frame = buffers[source]
                if frame is not None and len(frame) >= 6:
                    crc = 0xffff
                    for value in frame:
                        crc ^= value
                        for _ in range(8):
                            crc = (crc >> 1) ^ (0x8408 if crc & 1 else 0)
                    # A complete unfragmented LEAVE(slot1), inside IrLMP and IrLAP.
                    if source == side and crc == 0xf0b8 and not frame[1] & 1 and frame[4:-2] == bytes([2, 10, 1]):
                        return True
                buffers[source] = None
            elif buffers[source] is not None:
                if escaped[source]:
                    buffers[source].append(byte ^ 0x20)
                    escaped[source] = False
                elif byte == 0x7d:
                    escaped[source] = True
                else:
                    buffers[source].append(byte)
    return False


def analyse(out, args):
    logs = [(out / str(i) / 'PalmBoloLog.txt').read_text() for i in range(2)]
    errors, summaries = [], []
    for i, log in enumerate(logs):
        sums = [int(n) for n in re.findall(r'net: (\d+) checksums agree', log)]
        keys = re.findall(r'tick (\d+) keys ([0-9a-f]+) tank (\d+),(\d+)', log)
        positions = {(x, y) for _, _, x, y in keys}
        summary = dict(checksums=max(sums, default=0),
                       nonzero_inputs=sum(int(k, 16) != 0 for _, k, _, _ in keys),
                       positions=len(positions),
                       fragments=max(map(int, re.findall(r'fragments (\d+)', log)), default=0),
                       maxwire=max(map(int, re.findall(r'maxwire (\d+)', log)), default=0),
                       maxframe=max(map(int, re.findall(r'maxframe (\d+)', log)), default=0),
                       ended=[int(t) for t in re.findall(r'net: game over at tick (\d+), ended', log)])
        summaries.append(summary)
        started = 'net: start' if i == 0 else 'net: joined'
        ready = out / str(i) / 'ready-log.pdb'
        startup = ready.read_bytes().decode('latin1') if ready.is_file() else log
        if started not in startup:
            errors.append(f'{i}: did not enter multiplayer play')
        if summary['checksums'] < args.min_sums:
            errors.append(f'{i}: only {summary["checksums"]} checksum agreements')
        if 'out of step' in log:
            errors.append(f'{i}: desync')
        expected_stop = 'net: stopped (2)' if args.expect == 'host-leave' and i == 1 else None
        if 'net: stopped' in log and not (expected_stop and expected_stop in log):
            errors.append(f'{i}: unexpected stop')
        if ' to a bot at tick' in log and not (args.expect == 'join-leave' and i == 0):
            errors.append(f'{i}: unexpected bot takeover')
        if args.inputs and (not summary['nonzero_inputs'] or summary['positions'] < 2):
            errors.append(f'{i}: movement/input evidence missing')
    if args.expect == 'timed':
        if not summaries[0]['ended'] or summaries[0]['ended'] != summaries[1]['ended']:
            errors.append('both devices did not finish at the same tick')
    if args.expect == 'join-leave' and args.trace_relay and not leave_on_wire(out / 'relay.log', 1):
        errors.append('no valid joiner LEAVE frame observed on the wire')
    if args.expect == 'join-leave' and 'net: tank 1 to a bot at tick' not in logs[0]:
        errors.append('host did not take over the leaving joiner')
    if args.expect == 'host-leave' and 'net: stopped (2)' not in logs[1]:
        errors.append('joiner did not report HOST_LEFT')
    if args.map_pdb:
        ready = out / '1' / 'ready-log.pdb'
        map_log = logs[1] + (ready.read_bytes().decode('latin1') if ready.is_file() else '')
        if 'net: getting map' not in map_log or 'net: map kept as' not in map_log:
            errors.append('joiner did not fetch and keep the host map')
        got = out / '1' / 'retained-map.pdb'
        if not got.is_file() or pdb_payload(got) != pdb_payload(args.map_pdb):
            errors.append('retained map bytes differ or map export is missing')
    if args.require_fragments and args.map_pdb and summaries[0]['maxframe'] != 64:
        errors.append('map transfer did not exercise a 64-byte link frame plus length byte')
    if args.max_wire and any(not 0 < s['maxwire'] <= args.max_wire for s in summaries):
        errors.append('effective IR packet limit exceeded')
    if args.require_fragments and not summaries[0]['fragments']:
        errors.append('host did not record fragmented frames')
    result = dict(passed=not errors, errors=errors, devices=summaries)
    (out / 'result.json').write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps(result), flush=True)
    return not errors


def motion(core, folder, duration_ms, side):
    lines = []
    for start in range(0, max(1, int(duration_ms) - 6000), 15000):
        turn = 'hard1' if side == 0 else 'hard2'
        lines += [f'{start} key-down up', f'{start + 250} key-down hard3',
                  f'{start + 2250} key-up hard3', f'{start + 3000} key-down {turn}',
                  f'{start + 4500} key-up {turn}', f'{start + 6000} key-up up']
    core.replay(folder, lines)


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
    parser.add_argument('--tx-session', type=Path, help='use a calibrated TX launcher snapshot with private media copies')
    parser.add_argument('--arm-engine', choices=['exact', 'fast'], default='exact')
    parser.add_argument('--core-arm', type=Path, default=palm_boot.CORE)
    parser.add_argument('--host-prc', type=Path)
    parser.add_argument('--join-prc', type=Path)
    parser.add_argument('--map-pdb', type=Path, help='install only on host and select its second map row')
    parser.add_argument('--inputs', action='store_true')
    parser.add_argument('--input-seconds', type=float, help='stop input before a timed game finishes')
    parser.add_argument('--expect', choices=['play', 'timed', 'join-leave', 'host-leave'], default='play')
    parser.add_argument('--min-sums', type=int, default=2)
    parser.add_argument('--require-fragments', action='store_true')
    parser.add_argument('--max-wire', type=int, help='assert the effective packet-size bound')
    parser.add_argument('--trace-relay', action='store_true')
    parser.add_argument('--leave-ui', action='store_true', help='try the game menu Title action after play')
    parser.add_argument('--prc', type=Path, default=ROOT / 'build/PalmBolo.prc')
    parser.add_argument('--core-68k', type=Path, default=palm_boot.CORE_68K)
    parser.add_argument('--iiix-session', type=Path,
                        help='use this launcher session instead of the cached IIIx session')
    args = parser.parse_args()
    if args.leave_ui and args.expect not in ('join-leave', 'host-leave'):
        parser.error('--leave-ui requires --expect join-leave or host-leave')
    if args.host == args.join == 'tx':
        parser.error('TX/TX qualification needs a non-disruptive log reader; use an IIIx or mixed pair')
    if 'tx' in (args.host, args.join) and args.expect != 'timed':
        parser.error('TX qualification requires --expect timed to avoid exporting a live IR game')
    if args.ports[0] == args.ports[1]:
        parser.error('the IR ports must differ')
    for path in [args.prc, args.core_68k, args.core_arm, args.iiix_session, args.tx_session, args.map_pdb, args.host_prc, args.join_prc]:
        if path is not None and not path.is_file():
            parser.error(f'file does not exist: {path}')
    palm_boot.CORE_68K = args.core_68k.resolve()
    palm_boot.CORE = args.core_arm.resolve()
    out = args.out.resolve()
    out.mkdir(parents=True, exist_ok=True)
    if any(out.iterdir()):
        parser.error('output folder must be empty')
    provenance = dict(machine=platform.node(), host=args.host, join=args.join, speed=args.speed,
                      seconds=args.seconds, lobby_seconds=args.lobby_seconds, inputs=args.inputs, expect=args.expect, arm_engine=args.arm_engine)
    for name, path in [('core_68k', args.core_68k), ('core_arm', args.core_arm), ('prc', args.prc), ('host_prc', args.host_prc), ('join_prc', args.join_prc), ('map_pdb', args.map_pdb), ('tx_session', args.tx_session)]:
        if path is None:
            continue
        provenance[name] = dict(path=str(path.resolve()),
                               sha256=hashlib.sha256(path.read_bytes()).hexdigest())
    commit = args.core_68k.resolve().parent / 'COMMIT'
    if commit.is_file():
        provenance['frond_commit'] = commit.read_text().strip()
    (out / 'provenance.json').write_text(json.dumps(provenance, indent=2) + '\n')
    cores, kinds = [], [args.host, args.join]
    prcs = [args.host_prc or args.prc, args.join_prc or args.prc]
    relay = None
    relay_log = None
    try:
        for i, kind in enumerate(kinds):
            folder = out / str(i)
            folder.mkdir(exist_ok=True)
            print(f'Starting {kind} on IR port {args.ports[i]}, waiting for a heavy emulator slot.', flush=True)
            if kind == 'iiix':
                if args.iiix_session:
                    session = args.iiix_session
                else:
                    iiix = load_iiix()
                    session = iiix.session_for(iiix.ROM)
                shutil.copyfile(session, folder / 'launcher.frond')
                boot = [folder / 'launcher.frond', '--device', 'palm-iiix', '--speed', args.speed]
            else:
                if args.tx_session:
                    (folder / 'machine').mkdir()
                    boot = [args.tx_session.resolve(), '--device', 'palm-tx',
                            '--work-folder', folder / 'machine', '--engine', args.arm_engine]
                else:
                    images = Path(os.environ.get('PALMBOLO_TX_IMAGES', '/srv/downloads/temp/debug/palm/frond'))
                    shutil.copyfile(images / 'Palm-TX-5.4-nor.bin', folder / 'input-nor.bin')
                    shutil.copyfile(images / 'tx-calibrated.bin', folder / 'input-nand.bin')
                    boot = ['--device', 'palm-tx', folder / 'input-nor.bin', '--nand', folder / 'input-nand.bin',
                            '--engine', args.arm_engine]
            core = palm_boot.Core([*boot, '--irda', f'tcp:{args.ports[i]}'], folder / 'core.log')
            cores.append(core)
            if kind == 'iiix':
                core.send('speed-state', 'speed:', args.startup_timeout)
            else:
                if not args.tx_session:
                    core.wait_frame(folder / 'boot-frame.png', lambda rows: rows is not None and palm_boot.date_time_prefs(rows), 180)
                    core.key(folder, 'home', hold_ms=600)
                core.wait_frame(folder / 'launcher.png', palm_boot.launcher_wallpaper, 120)
            result = core.send(f'install "{prcs[i].resolve()}"', ('installation successful', 'installation failed', 'failed to install'), 120)
            if 'successful' not in result:
                raise RuntimeError(result)
            if i == 0 and args.map_pdb:
                result = core.send(f'install \"{args.map_pdb.resolve()}\"', ('installation successful', 'installation failed', 'failed to install'), 120)
                if 'successful' not in result:
                    raise RuntimeError(result)
        relay_log = (out / 'relay.log').open('w')
        relay_path = ROOT / 'tools/frond-ir-relay.py' if args.trace_relay else FROND / 'tools/link-irda.py'
        relay = subprocess.Popen([sys.executable, str(relay_path),
                                  *map(str, args.ports)], stdout=relay_log, stderr=subprocess.STDOUT)
        for i, core in enumerate(cores):
            core.launch('PalmBolo')
            core.frame(out / str(i) / 'title.png')

        def tap(i, low, high, hold_ms=None):
            cores[i].tap(out / str(i), *(low if kinds[i] == 'iiix' else high),
                         hold_ms=hold_ms if hold_ms is not None else (150 if kinds[i] == 'iiix' else 800))
            time.sleep(1)

        tap(0, (40, 119), (160, 246))
        cores[0].frame(out / '0' / 'map-list.png')
        if args.map_pdb:
            tap(0, (50, 33), (100, 66))
            cores[0].frame(out / '0' / 'map-selected.png')
        tap(0, (30, 150), (68, 428))
        cores[0].frame(out / '0' / 'new-game.png')
        tap(0, (80, 150), (160, 428))
        tap(1, (120, 119), (160, 286))
        tap(1, (30, 150), (68, 428))
        time.sleep(args.lobby_seconds)
        for i, core in enumerate(cores):
            core.frame(out / str(i) / 'lobby.png')
        tap(0, (30, 150), (68, 428))
        time.sleep(6)
        if args.inputs:
            # TX exports run guest database calls and can starve the live IR link.
            # In mixed pairs the IIIx log confirms play; retain TX logs at the end.
            for i, core in enumerate(cores):
                if kinds[i] == 'iiix':
                    wait_play(core, kinds[i], out / str(i))
            for i, core in enumerate(cores):
                if kinds[i] == 'tx' and 'iiix' in kinds:
                    core.frame(out / str(i) / 'ready.png')
                duration = args.input_seconds if args.input_seconds is not None else max(8, args.seconds - 6)
                motion(core, out / str(i), duration * 1000, i)
        remaining = max(0, args.seconds - 6)
        while remaining:
            wait = min(30, remaining)
            time.sleep(wait)
            remaining -= wait
            print(f'Play observation: {args.seconds - remaining:.0f}/{args.seconds:.0f} seconds', flush=True)
        if args.leave_ui:
            side = 1 if args.expect == 'join-leave' else 0
            tap(side, (15, 202), (54, 464), hold_ms=800)
            time.sleep(3)
            cores[side].frame(out / str(side) / 'menu.png')
            tap(side, (20, 18), (40, 36), hold_ms=800)
            time.sleep(10)
        tx_finished = False
        if 'tx' in kinds:
            witness = kinds.index('iiix')
            log = export_log(cores[witness], 'iiix', out / str(witness))
            if not log.is_file() or not re.search(rb'net: game over at tick \d+, ended', log.read_bytes()):
                raise RuntimeError('timed game is not finished; refusing live TX database export')
            # Let the peer finish its bounded end/close handshake too.
            time.sleep(10)
            tx_finished = True
        for i, core in enumerate(cores):
            folder = out / str(i)
            core.frame(folder / 'game.png')
            core.send('irda-stats', ('rx=', 'infrared', 'invalid command'), 30)
            export_log(core, kinds[i], folder, tx_finished=tx_finished)
            archive = folder / 'databases.zip'
            log = folder / 'PalmBoloLog.pdb'
            if args.map_pdb and i == 1:
                map_name = args.map_pdb.read_bytes()[:32].split(b'\0')[0].decode('latin-1')
                if kinds[i] == 'iiix':
                    core.send(f'db-export-db \"{map_name}\" \"{folder / "retained-map.pdb"}\"', ('export written', 'export failed'), 60)
                elif archive.is_file():
                    with zipfile.ZipFile(archive) as data:
                        for name in data.namelist():
                            if Path(name).stem == map_name:
                                (folder / 'retained-map.pdb').write_bytes(data.read(name))
            if log.is_file():
                subprocess.run([sys.executable, str(ROOT / 'tools/dumplog.py'), str(log)], check=True,
                               stdout=subprocess.DEVNULL)
            else:
                print(f'{i}: no app log exported; see core.log', flush=True)
        if not analyse(out, args):
            raise RuntimeError('IR qualification assertions failed; see result.json')
    except Exception as error:
        if not (out / 'result.json').exists():
            (out / 'result.json').write_text(json.dumps(dict(passed=False,
                errors=[str(error)], outcome='incomplete'), indent=2) + '\n')
        raise
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
            shutil.rmtree(out / str(i) / 'machine', ignore_errors=True)
            for replay in (out / str(i)).glob('console-*.replay'):
                replay.unlink()


if __name__ == '__main__':
    main()
