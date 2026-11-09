# FFmpeg protocol patch — where the KDSP integration goes

> **Status: implemented and verified (2026-11-09).** The protocol and
> the function surface (`kdsp_open / kdsp_read / kdsp_seek / kdsp_close`
> plus the `kdsp_average_speed / kdsp_realtime_speed` helpers) were
> specified by the repository owner; the implementation in this
> directory was written with AI assistance at the owner's direction and
> then verified two ways: (1) built into ijkplayer's FFmpeg for
> arm64 — `CONFIG_KDSP_PROTOCOL 1`, `ff_kdsp_protocol` and both speed
> helpers exported from `libijkffmpeg.so`; (2) exercised on the host
> against the real Python server — see `../../tests/host/`
> (byte-identical clean-stream reconstruction, sequence-gap counting
> under `--drop 0.5`, and garbage-prefix resync).

## Goal

Make ijkplayer open `kdsp://host:port` natively, by adding a `kdsp`
**URLProtocol** to the FFmpeg tree that ijkplayer builds (libavformat),
following the receiver rules in [../../protocol.md](../../protocol.md):

- `kdsp_open`: parse the URL, open the TCP connection.
- `kdsp_read`: read one KDSP frame, verify/strip the 22-byte header,
  resynchronise on a bad magic by scanning for the next `KDSP`, return the
  Annex B payload bytes to the H.264 demuxer.
- Sequence tracking inside the protocol context; expose counters
  (received / dropped) the same way other ijk/FFmpeg stats are surfaced, so
  the demo app can display them.
- Short read / EOF handling that lets ijkplayer's reconnect logic engage
  instead of hanging when the server dies.

## Files to touch (verified 2026-11-09 against the actual ijk FFmpeg tree, ff3.4--ijk0.8.7--20180103--001 — see `docs/build-companion.md`)

| File | Change |
|---|---|
| `libavformat/kdsp.c` | **New.** The URLProtocol implementation (~250 lines). Minimal model: `data_uri.c`; network model: `tcp.c`. |
| `libavformat/protocols.c` | Add `extern const URLProtocol ff_kdsp_protocol;` — configure auto-generates the protocol list from this file (`find_things_extern`), so **no configure list edit is needed**. |
| `libavformat/Makefile` | Add `OBJS-$(CONFIG_KDSP_PROTOCOL) += kdsp.o` (next to the tcp.o line). |
| ijkplayer FFmpeg build config | Nothing to change: the default `config/module-lite.sh` starts from `--enable-protocols`, so kdsp is built unless explicitly disabled. Verify after configure: `CONFIG_KDSP_PROTOCOL 1` in the generated `config.h`. |

Note the patch applies to the per-arch FFmpeg copy ijkplayer builds from
(`android/contrib/ffmpeg-arm64`), not to a random upstream FFmpeg checkout.

## Deliverable form

The finished work lands here as source files plus a patch file, not as
a vendored FFmpeg tree:

```
kdsp.c                                    the protocol implementation
kdsp.h                                    the two speed-helper declarations
0001-libavformat-add-kdsp-protocol.patch  all four file changes as one diff
                                          (kdsp.c, kdsp.h, protocols.c, Makefile)
```

(No separate "enable in module" patch is needed: the default lite
module enables all protocols, verified by `CONFIG_KDSP_PROTOCOL 1` in
the generated `config.h`.)

Keeping changes as patches makes the diff reviewable in minutes — which is
the point of this repository — and keeps LGPL obligations for the modified
FFmpeg copy straightforward (see `../../NOTICE.md`).

## Phase 2 (optional, after the first public release)

A separate `kdsp` **demuxer** (AVInputFormat) that turns KDSP frames
directly into AVPackets with PTS derived from the header timestamp, plus
AAC audio frames. Tracked as a second patch; not required for the v1 demo.
