# FFmpeg protocol patch — where the KPLD integration goes

> **Status: TODO — this is the core step, intentionally left for the
> repository owner.** Everything else in this repo (spec, server, app,
> build) is scaffolding. This patch is the proof of skill, so it is not
> scaffolded or generated; this file only records the agreed design.

## Goal

Make ijkplayer open `kpld://host:port` natively, by adding a `kpld`
**URLProtocol** to the FFmpeg tree that ijkplayer builds (libavformat),
following the receiver rules in [../../protocol.md](../../protocol.md):

- `kpld_open`: parse the URL, open the TCP connection.
- `kpld_read`: read one KPLD frame, verify/strip the 22-byte header,
  resynchronise on a bad magic by scanning for the next `KPLD`, return the
  Annex B payload bytes to the H.264 demuxer.
- Sequence tracking inside the protocol context; expose counters
  (received / dropped) the same way other ijk/FFmpeg stats are surfaced, so
  the demo app can display them.
- Short read / EOF handling that lets ijkplayer's reconnect logic engage
  instead of hanging when the server dies.

## Files to touch (confirm exact paths against the k0.8.8 tree when building)

| File | Change |
|---|---|
| `libavformat/kpld.c` | **New.** The URLProtocol implementation (~250 lines). |
| `libavformat/Makefile` | Register `kpld.o` under the kpld protocol. |
| `libavformat/allformats.c` (or the tree's registration mechanism) | Register `ff_kpld_protocol`. |
| `libavformat/protocols.c` / configure lists, if the tree requires it | Enable `CONFIG_KPLD_PROTOCOL`. |
| ijkplayer FFmpeg build config (`module-lite*.sh` / ijk ffmpeg configure flags) | Make sure the protocol is not compiled out by the lite module list. |

## Deliverable form

The finished work lands here as patch files, not as a vendored FFmpeg tree:

```
0001-libavformat-add-kpld-protocol.patch
0002-ijkplayer-enable-kpld-in-lite-module.patch
```

Keeping changes as patches makes the diff reviewable in minutes — which is
the point of this repository — and keeps LGPL obligations for the modified
FFmpeg copy straightforward (see `../../NOTICE.md`).

## Phase 2 (optional, after the first public release)

A separate `kpld` **demuxer** (AVInputFormat) that turns KPLD frames
directly into AVPackets with PTS derived from the header timestamp, plus
AAC audio frames. Tracked as a second patch; not required for the v1 demo.
