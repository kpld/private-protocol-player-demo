#!/usr/bin/env python3
"""KDSP demo streaming server.

Reads a raw H.264 elementary stream (with AUD NAL units, see
generate_sample.sh), packetises it into KDSP frames (see ../protocol.md)
and streams it over TCP, paced at the stream's frame rate, looping forever.

Weak-network simulation:
  --drop 0.05    drop 5% of frames (the player's loss counter should move)
  --jitter 40    add +/-40 ms random jitter to the send pacing
  --delay 200    add a fixed 200 ms delay before each frame

Usage:
  python3 kdsp_server.py --file sample.h264 --port 19350

Ctrl+C stops the server. The stop works by polling a flag set by the
SIGINT handler (the accept loop and the pacing sleeps wake up
periodically to check it), because a blocking accept() cannot be
interrupted by Ctrl+C on Windows at all, and only unreliably elsewhere.
"""
import argparse
import random
import signal
import socket
import struct
import sys
import threading
import time

STOP = threading.Event()  # set by the SIGINT handler; polled by every wait


def _on_sigint(signum, frame):
    STOP.set()


MAGIC = b"KDSP"  # 4 ASCII bytes on the wire, in this order (spec: magic field)
VERSION = 0x01
HEADER = struct.Struct("<4sBBIQI")  # magic, version, flags, sequence, timestamp_ms, payload_length
FLAG_KEYFRAME = 0x01


def split_access_units(data: bytes):
    """Split an Annex B H.264 stream into access units at AUD boundaries.

    The sample generator inserts AUDs (x264 aud=1). Each returned AU starts
    with a start code. If the stream has no AUDs we fall back to treating
    every VCL NAL as its own AU, which is wrong for multi-slice frames but
    keeps the server usable for simple streams.
    """
    # Find all start codes (3- or 4-byte) with a C-speed regex scan; a
    # byte-by-byte Python loop over tens of MB is far too slow.
    import re
    n = len(data)
    nals = []  # (start_code_begin, nal_header_offset, nal_type)
    for m in re.finditer(rb"\x00\x00(?:\x00)?\x01", data):
        hdr = m.end()
        nal_type = data[hdr] & 0x1F if hdr < n else 0
        nals.append((m.start(), hdr, nal_type))
    if not nals:
        raise ValueError("no Annex B start codes found — is this a raw .h264 file?")

    bounds = []
    for idx, (begin, _hdr, nal_type) in enumerate(nals):
        end = nals[idx + 1][0] if idx + 1 < len(nals) else n
        bounds.append((begin, end, nal_type))

    aus, current = [], []
    has_aud = any(t == 9 for _, _, t in bounds)
    for begin, end, nal_type in bounds:
        chunk = data[begin:end]
        if has_aud:
            if nal_type == 9 and current:
                aus.append(b"".join(current))
                current = []
            current.append(chunk)
        else:
            if nal_type in (1, 5) and current:
                aus.append(b"".join(current))
                current = []
            current.append(chunk)
    if current:
        aus.append(b"".join(current))

    def is_keyframe(au: bytes) -> bool:
        return b"\x00\x00\x00\x01\x65" in au or b"\x00\x00\x01\x65" in au

    return [(au, is_keyframe(au)) for au in aus]


def serve_client(conn, aus, args, rng):
    frame_interval = 1.0 / args.fps
    seq = 0
    sent = dropped = 0
    print(f"[server] client connected, streaming {len(aus)} frames in a loop", flush=True)
    # A short send timeout keeps any single sendall() from blocking for
    # long; on timeout we just re-check the stop flag and carry on.
    conn.settimeout(2.0)
    try:
        while not STOP.is_set():
            for au, keyframe in aus:
                if STOP.is_set():
                    break
                t0 = time.monotonic()
                if rng.random() < args.drop:
                    dropped += 1
                    seq += 1
                    continue
                if args.delay and STOP.wait(args.delay / 1000.0):
                    break
                ts_ms = int(time.time() * 1000)
                header = HEADER.pack(
                    MAGIC, VERSION, FLAG_KEYFRAME if keyframe else 0,
                    seq & 0xFFFFFFFF, ts_ms, len(au),
                )
                while True:
                    try:
                        conn.sendall(header + au)
                        break
                    except socket.timeout:
                        if STOP.is_set():
                            break
                if STOP.is_set():
                    break
                sent += 1
                seq += 1
                elapsed = time.monotonic() - t0
                jitter = rng.uniform(-args.jitter, args.jitter) / 1000.0 if args.jitter else 0.0
                # STOP.wait doubles as the pacing sleep: it returns the
                # instant Ctrl+C is pressed instead of sleeping through it.
                if STOP.wait(max(0.0, frame_interval - elapsed + jitter)):
                    break
    except (BrokenPipeError, ConnectionResetError):
        print(f"[server] client disconnected (sent={sent}, dropped={dropped})", flush=True)
        return
    if STOP.is_set():
        print(f"[server] interrupted (sent={sent}, dropped={dropped})", flush=True)


def main():
    ap = argparse.ArgumentParser(description="KDSP demo streaming server")
    ap.add_argument("--file", default="sample.h264", help="raw H.264 elementary stream with AUDs")
    ap.add_argument("--port", type=int, default=19350)
    ap.add_argument("--fps", type=float, default=25.0, help="send pacing (match the sample's frame rate)")
    ap.add_argument("--drop", type=float, default=0.0, help="frame drop probability, e.g. 0.05")
    ap.add_argument("--jitter", type=float, default=0.0, help="+/- jitter in ms")
    ap.add_argument("--delay", type=float, default=0.0, help="fixed extra delay per frame in ms")
    args = ap.parse_args()

    with open(args.file, "rb") as f:
        data = f.read()
    aus = split_access_units(data)
    print(f"[server] loaded {args.file}: {len(data)} bytes, {len(aus)} access units", flush=True)

    # Ctrl+C only sets the STOP flag (see module docstring): every
    # blocking wait below polls it, so the stop works on Windows too,
    # where a blocking accept() never raises KeyboardInterrupt.
    signal.signal(signal.SIGINT, _on_sigint)

    rng = random.Random(20261109)
    srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    srv.bind(("0.0.0.0", args.port))
    srv.listen(1)
    srv.settimeout(0.5)  # accept() wakes up twice a second to poll STOP
    print(f"[server] listening on 0.0.0.0:{args.port} (kdsp://<this-host>:{args.port})", flush=True)
    try:
        while not STOP.is_set():
            try:
                conn, addr = srv.accept()
            except socket.timeout:
                continue
            print(f"[server] connection from {addr}", flush=True)
            with conn:
                serve_client(conn, aus, args, rng)
    except KeyboardInterrupt:
        pass  # backstop for platforms where Ctrl+C does raise
    finally:
        srv.close()
    if STOP.is_set():
        print("\n[server] stopped by Ctrl+C", flush=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
