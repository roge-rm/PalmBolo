#!/usr/bin/env python3
"""Run Frond's relay with a timestamped byte trace for PalmBolo qualification."""
import json
import os
import runpy
import socket
import sys
import time
from pathlib import Path

connect = socket.create_connection
ports = [int(value) for value in sys.argv[1:3]]
started = time.monotonic()


class TracedSocket:
    def __init__(self, peer, source):
        self.peer, self.source = peer, source

    def __getattr__(self, name):
        return getattr(self.peer, name)

    def recv(self, size, *args):
        data = self.peer.recv(size, *args)
        if data:
            print(json.dumps(dict(t=time.monotonic() - started,
                                  source=self.source, hex=data.hex())), flush=True)
        return data


def traced_connect(address, *args, **kwargs):
    return TracedSocket(connect(address, *args, **kwargs), ports.index(address[1]))


socket.create_connection = traced_connect
runpy.run_path(str(Path(os.environ.get('PALMBOLO_FROND_ROOT', Path.home() / 'Projects/palm/frond')) / 'tools/link-irda.py'), run_name='__main__')
