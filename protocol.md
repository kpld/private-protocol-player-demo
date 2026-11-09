# KPLD Protocol — Specification v1 (fictional, for demo purposes)

KPLD is a **fictional** proprietary-style streaming protocol invented for this
demo repository. It does not correspond to any real vendor protocol. Its only
job is to be simple, realistic, and impossible for a stock player to open —
so the FFmpeg/ijkplayer integration work is genuinely necessary.

## Transport

- TCP, default port **19350**
- One connection carries one video stream (phase 1). Audio (AAC) is phase 2.
- The server starts sending as soon as a client connects; there is no
  handshake in v1. An auth/token field is reserved in the header flags area
  for future use (see "Reserved").

## Frame format

Every frame is a fixed 22-byte little-endian header followed by the payload.

| Offset | Field          | Size | Description                                        |
|-------:|----------------|-----:|----------------------------------------------------|
| 0      | magic          | 4 B  | `0x4B504C44` — ASCII `KPLD`. Used for resync.      |
| 4      | version        | 1 B  | `0x01`                                             |
| 5      | flags          | 1 B  | bit0: keyframe. Other bits reserved, must be 0.    |
| 6      | sequence       | 4 B  | uint32 LE, increments by 1 per frame, wraps.       |
| 10     | timestamp_ms   | 8 B  | uint64 LE, sender clock, milliseconds.             |
| 18     | payload_length | 4 B  | uint32 LE, bytes of payload that follow.           |
| 22     | payload        | N B  | One H.264 access unit in Annex B format.           |

## Receiver rules (what the player-side protocol code must do)

1. Read exactly 22 header bytes; if `magic` does not match, scan forward one
   byte at a time until the next `KPLD` magic is found (resynchronisation
   after corruption or a mid-stream join).
2. Read exactly `payload_length` payload bytes. Sanity limit: 8 MiB; a larger
   declared length means the stream is desynchronised — resync instead of
   allocating.
3. Track `sequence`: a gap means dropped frame(s); count it in the stats,
   keep playing. Out-of-order frames cannot occur on TCP, but the counter is
   kept because real vendor protocols usually run over UDP as well.
4. `timestamp_ms` is the presentation clock of the sender. The demo player
   displays an end-to-end latency estimate = local receive time − timestamp
   (valid only when the two clocks are roughly synchronised, e.g. same LAN —
   this limitation is stated on purpose, it is what a real diagnosis would
   flag).

## Server behaviour

- `server/kpld_server.py` packetises a raw H.264 elementary stream file
  (`sample.h264`, see `server/generate_sample.sh`) into KPLD frames and sends
  them paced at the stream's frame rate, looping forever.
- Weak-network simulation flags (server side): `--drop 0.05` drops 5% of
  frames, `--jitter 40` adds ±40 ms send jitter, `--delay 200` adds a fixed
  200 ms delay. Used to demonstrate the player's loss counters and reconnect.

## Reserved / phase 2

- Audio frames: a future frame type field. In v1, audio is out of scope.
- Auth: a future HELLO frame sent by the client before the server starts
  streaming. Deliberately left out of v1 to keep the demo focused on the
  player-side protocol/demuxer integration.
