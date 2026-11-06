#!/usr/bin/env python3
"""Samples where PalmBolo spends its time on the Frond IIIx, through
frond-68k's GDB stub: stops the Palm every few ms of wall time, reads the PC
and walks the A6 frame chain, then lets it go. The guest waits while
stopped, so the samples are spread evenly over guest time.

A PC in the app counts for its function. A PC in the ROM counts as "OS"
and for the first app function up its frame chain ("OS from Foo"). The
app's code is found by matching the code bytes at each PC against the
sections of build/debug/palmbolo, so no load addresses are needed.

Usage: iiix-profile.py OUTDIR --replay R --start S [--samples N]
       [--every SECONDS] [--prc PRC] [--exe EXE]
Waits S seconds after launch (with the replay running), then takes N
samples and writes OUTDIR/profile.txt. Each sample takes about a second of
wall time, as the stub answers slowly; the Palm only runs between them.
"""
import argparse
import collections
import re
import socket
import subprocess
import sys
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent
sys.path.insert(0, str(HERE))
iiix = __import__('frond-iiix')
palm_boot = iiix.palm_boot
PORT = 41259


class Rsp:
    def __init__(self, port):
        for _ in range(50):
            try:
                self.s = socket.create_connection(('127.0.0.1', port), timeout=30)
                break
            except OSError:
                time.sleep(0.2)
        self.buf = b''

    def _byte(self):
        if not self.buf:
            self.buf = self.s.recv(4096)
            if not self.buf:
                raise EOFError
        b, self.buf = self.buf[:1], self.buf[1:]
        return b

    def read_packet(self):
        while self._byte() != b'$':
            pass
        data = b''
        while True:
            b = self._byte()
            if b == b'#':
                break
            data += b
        self._byte(), self._byte()
        self.s.sendall(b'+')
        return data.decode()

    def send(self, data, reply=True):
        cs = sum(data.encode()) & 0xFF
        self.s.sendall(f'${data}#{cs:02x}'.encode())
        return self.read_packet() if reply else None

    def regs(self):
        g = self.send('g')
        return [int(g[i * 8:i * 8 + 8], 16) for i in range(18)]

    def mem(self, addr, n):
        r = self.send(f'm{addr:x},{n:x}')
        return bytes.fromhex(r) if r and not r.startswith('E') else b''

    def stop(self):
        self.s.sendall(b'\x03')
        return self.read_packet()


def sections(exe):
    """Code sections of the COFF file: name -> bytes, and their symbols."""
    out = subprocess.run(['m68k-palmos-objdump', '-h', str(exe)], capture_output=True, text=True).stdout
    lines = out.splitlines()
    names = []
    for i, l in enumerate(lines):
        if re.match(r'\s+\d+ ', l) and i + 1 < len(lines) and 'CODE' in lines[i + 1]:
            names.append((int(l.split()[0]), l.split()[1]))
    names = sorted(names)
    code, syms = {}, collections.defaultdict(list)
    for _, n in names:
        tmp = ROOT / 'build' / f'.sec-{n.strip(".")}.bin'
        subprocess.run(['m68k-palmos-objcopy', '-O', 'binary', '-j', n, str(exe), str(tmp)], check=True)
        code[n] = tmp.read_bytes()
        tmp.unlink()
    t = subprocess.run(['m68k-palmos-objdump', '-t', str(exe)], capture_output=True, text=True).stdout
    for l in t.splitlines():
        m = re.match(r'\[\s*\d+\]\(sec\s+(\d+)\).*0x([0-9a-f]+)\s+(\S+)$', l)
        if m and not m.group(3).startswith(('.', '__gnu', 'gcc2_')):
            syms[int(m.group(1))].append((int(m.group(2), 16), m.group(3)))
    return code, names, syms


def main():
    p = argparse.ArgumentParser()
    p.add_argument('out', type=Path)
    p.add_argument('--replay', type=Path, required=True)
    p.add_argument('--start', type=float, default=25)
    p.add_argument('--samples', type=int, default=150)
    p.add_argument('--every', type=float, default=0.1, help='seconds the Palm runs between samples')
    p.add_argument('--prc', type=Path, default=ROOT / 'build/PalmBolo.prc')
    p.add_argument('--exe', type=Path, default=ROOT / 'build/debug/palmbolo')
    a = p.parse_args()
    out = a.out.resolve()
    out.mkdir(parents=True, exist_ok=True)
    session = iiix.session_for(iiix.ROM)
    core = palm_boot.Core([session, '--device', 'palm-iiix', '--speed', 'original', '--gdb', str(PORT)],
                          out / 'core.log')
    samples = []
    mem = {}
    try:
        core.until('speed-state', 'speed:', 'speed', 30)
        reply = core.send(f'install "{a.prc.resolve()}"', 'install', 60)
        if 'successful' not in reply:
            raise SystemExit(f'install: {reply.strip()}')
        core.launch('PalmBolo')
        # Without the replay's snapshots, which would land in the core's folder.
        lines = [l for l in a.replay.read_text().splitlines() if ' snapshot ' not in l]
        (out / 'input.replay').write_text('\n'.join(lines) + '\n')
        core.send(f'input-replay "{out / "input.replay"}"', 'input replay loaded')
        time.sleep(a.start)
        r = Rsp(PORT)
        # Halt it (ignored if the stub stopped it on connect), drop any stop
        # packet, then ask where it is.
        r.s.sendall(b'\x03')
        time.sleep(0.5)
        r.s.settimeout(0.3)
        try:
            while True:
                r.read_packet()
        except (socket.timeout, TimeoutError):
            pass
        r.s.settimeout(30)
        r.buf = b''
        r.send('?')
        for _ in range(a.samples):
            regs = r.regs()
            pc, a6 = regs[17], regs[14]
            chain = []
            # Only an OS PC needs the chain, to find the app function above.
            for _ in range(8 if pc >= 0x10000000 else 0):
                if not a6 or a6 & 1:
                    break
                fr = r.mem(a6, 8)
                if len(fr) < 8:
                    break
                a6, ret = int.from_bytes(fr[:4], 'big'), int.from_bytes(fr[4:], 'big')
                chain.append(ret)
            samples.append((pc, chain))
            r.send('c', reply=False)
            time.sleep(a.every)
            r.stop()
        # Code bytes at every PC seen, read while stopped.
        for pc, chain in samples:
            for x in [pc] + chain:
                if x not in mem and x < 0x10000000:
                    mem[x] = r.mem(x, 16)
        r.send('D', reply=True)
    finally:
        core.close()
        (out / 'input.replay').unlink(missing_ok=True)
    code, names, syms = sections(a.exe)

    def where(x):
        """(function, section) for an app address, by its code bytes."""
        b = mem.get(x, b'')
        if len(b) < 12:
            return None
        for idx, n in names:
            k = code[n].find(b[:12])
            if k >= 0 and code[n].find(b[:12], k + 1) < 0:
                fn = max((s for s in syms.get(idx + 1, []) if s[0] <= k), default=(0, '?'))[1]
                return f'{fn} [{n}]'
        return None

    flat, incl = collections.Counter(), collections.Counter()
    for pc, chain in samples:
        w = where(pc)
        caller = next((c for c in (where(x) for x in chain) if c), None)
        if w:
            flat[w] += 1
        else:
            flat[f'OS from {caller}' if caller else 'OS'] += 1
        seen = set()
        for x in [pc] + chain:
            c = where(x)
            if c and c not in seen:
                seen.add(c)
                incl[c] += 1
    n = len(samples)
    lines = [f'{n} samples, the Palm running {a.every * 1000:.0f} ms of wall time between them', '',
             'Self (PC in the function, or the OS called from it):']
    lines += [f'{c * 100 / n:6.1f}%  {k}' for k, c in flat.most_common(40)]
    lines += ['', 'Inclusive (the function or anything it called):']
    lines += [f'{c * 100 / n:6.1f}%  {k}' for k, c in incl.most_common(40)]
    (out / 'profile.txt').write_text('\n'.join(lines) + '\n')
    print('\n'.join(lines[:30]))


if __name__ == '__main__':
    main()
