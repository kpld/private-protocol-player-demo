#!/usr/bin/env python3
"""KPLD protocol verification client (development tool).

Connects to a running kpld_server, parses frames exactly the way the
player-side protocol code must (see protocol.md "Receiver rules") and
reports: header sanity, sequence monotonicity, keyframe cadence, payload
start codes, and sender->receiver timestamp delta. Exits non-zero on the
first protocol violation.

Usage: python3 test_parse.py --host 127.0.0.1 --port 19350 --frames 50
"""
import argparse
import socket
import struct
import sys
import time

MAGIC = 0x4B504C44
HEADER = struct.Struct("<IBBIQI")


def recv_exact(sock, n):
    buf = bytearray()
    while len(buf) < n:
        chunk = sock.recv(n - len(buf))
        if not chunk:
            raise ConnectionError("server closed the connection")
        buf += chunk
    return bytes(buf)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--port", type=int, default=19350)
    ap.add_argument("--frames", type=int, default=50)
    args = ap.parse_args()

    sock = socket.create_connection((args.host, args.port), timeout=10)
    prev_seq = None
    keyframes = 0
    total_payload = 0
    deltas = []
    t_start = time.time()
    for i in range(args.frames):
        header = recv_exact(sock, HEADER.size)
        magic, version, flags, seq, ts_ms, plen = HEADER.unpack(header)
        if magic != MAGIC:
            print(f"FAIL frame {i}: bad magic 0x{magic:08X}")
            return 1
        if version != 1:
            print(f"FAIL frame {i}: bad version {version}")
            return 1
        if plen <= 0 or plen > 8 * 1024 * 1024:
            print(f"FAIL frame {i}: implausible payload_length {plen}")
            return 1
        payload = recv_exact(sock, plen)
        if not (payload.startswith(b"\x00\x00\x00\x01") or payload.startswith(b"\x00\x00\x01")):
            print(f"FAIL frame {i}: payload does not start with an Annex B start code")
            return 1
        if prev_seq is not None and seq != (prev_seq + 1) & 0xFFFFFFFF:
            print(f"NOTE frame {i}: sequence gap {prev_seq} -> {seq} (expected when server --drop is on)")
        prev_seq = seq
        if flags & 0x01:
            keyframes += 1
        total_payload += plen
        deltas.append(int(time.time() * 1000) - ts_ms)

    elapsed = time.time() - t_start
    print(f"OK: parsed {args.frames} frames in {elapsed:.2f}s")
    print(f"  keyframes: {keyframes}, total payload: {total_payload} bytes")
    print(f"  timestamp delta (receive - send): min {min(deltas)} ms, max {max(deltas)} ms")
    return 0


if __name__ == "__main__":
    sys.exit(main())
