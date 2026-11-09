# Adding a proprietary protocol to ijkplayer/FFmpeg — a working demo

Many hardware devices — IP cameras, video intercoms, drone video links,
recorders — do **not** stream plain RTSP. Their streams carry a proprietary
frame header, a private timestamp, sometimes a private transport. Stock
players (ExoPlayer, a vanilla ijkplayer build) cannot open them at all.

This repository demonstrates, end to end, how such a protocol gets integrated
into an FFmpeg-based Android player:

1. a fictional proprietary protocol (**KPLD**, spec in
   [protocol.md](protocol.md)) — invented for this demo, resembling what real
   device vendors ship;
2. a small server that streams it (`server/`);
3. the player-side integration in FFmpeg's protocol layer for ijkplayer, so
   the Android app can simply open `kpld://host:port` (`player/`);
4. a minimal Android demo app with live playback stats: first-frame time,
   estimated latency, frame rate, bitrate, dropped frames, auto-reconnect.

> Status: **skeleton / work in progress.** The protocol spec, the streaming
> server (tested), the Android app skeleton, and the build documentation are
> in place. The FFmpeg protocol implementation itself is the next step — see
> `player/ffmpeg-patch/README.md` for exactly where it goes and why.
> A standard player cannot play the demo stream; the patched player can.
> (Comparison screenshots land in `docs/screenshots/` once the player patch
> is built.)

## Quick start

```bash
# 1. Generate the sample stream (synthetic test pattern, no copyrighted media)
cd server && ./generate_sample.sh

# 2. Start the KPLD streaming server
python3 kpld_server.py --file sample.h264 --port 19350
#    weak-network demo:  python3 kpld_server.py --drop 0.05 --jitter 40

# 3. Build & install the Android demo app (see player/ijkplayer-build.md),
#    then enter:  kpld://<your-PC-LAN-IP>:19350
```

## What this proves

- **FFmpeg internals, not player APIs** — the integration point is a custom
  `URLProtocol` in libavformat (header parsing, resync on bad magic, sequence
  tracking), not a Java-level workaround.
- **Custom ijkplayer builds** — pinned, reproducible build of the
  ijkplayer/FFmpeg stack for arm64-v8a (see `player/ijkplayer-build.md` and
  `docker/`).
- **Live-stream behaviour** — reconnect after the server dies, loss/jitter
  statistics, latency estimation from the protocol timestamp.
- **Diagnosis-first working style** — the same steps, in the same order, that
  a real vendor-protocol integration follows: spec → server/simulator →
  protocol layer → player → stats.

## Repository layout

```
protocol.md            KPLD protocol specification (fictional)
server/                streaming server + sample generator (Python, tested)
player/ffmpeg-patch/   where the FFmpeg protocol code goes, and why there
player/app/            Android demo app (skeleton)
player/ijkplayer-build.md   reproducible custom build notes
docs/                  latency test notes, screenshots
docker/                frozen build environment for the native stack
```

## Licensing

Original code in this repository is MIT-licensed (see `LICENSE`).
It builds on **FFmpeg** and **ijkplayer**, which are **LGPL-2.1 or later**
components — see `NOTICE.md`. This demo is a proof of skill, not a
redistribution of either project.

## Need this for a real device protocol?

I take on a small number of fixed-price integrations of proprietary
streaming protocols into ijkplayer/FFmpeg-based Android players — starting
with a fixed-price feasibility diagnosis based on your protocol document and
a sample stream, before any development quote. (Contact via GitHub profile.)
