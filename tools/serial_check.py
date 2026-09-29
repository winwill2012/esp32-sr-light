"""Capture boot diagnostics without leaving the serial port open."""
import argparse
import time
from pathlib import Path
import serial

parser = argparse.ArgumentParser()
parser.add_argument('--port', default='COM3')
parser.add_argument('--seconds', type=float, default=25)
parser.add_argument('--no-reset', action='store_true')
args = parser.parse_args()
data = bytearray()
port = serial.Serial(port=None, baudrate=115200, timeout=0.25)
port.dtr = False
port.rts = False
port.port = args.port
with port:
    if not args.no_reset:
        port.rts = True
        time.sleep(0.15)
        port.rts = False
    deadline = time.monotonic() + args.seconds
    while time.monotonic() < deadline:
        chunk = port.read(8192)
        if chunk:
            data.extend(chunk)
            print(chunk.decode('utf-8', errors='replace'), end='', flush=True)
Path(__file__).with_name('serial.log').write_bytes(data)
