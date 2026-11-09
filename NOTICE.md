# NOTICE — third-party components

This repository demonstrates an integration technique. It does **not**
redistribute the following projects; you fetch them yourself when building
(see `player/ijkplayer-build.md`).

| Component | Upstream | License |
|---|---|---|
| FFmpeg | https://ffmpeg.org | LGPL-2.1 or later (some optional parts GPL) |
| ijkplayer | https://github.com/bilibili/ijkplayer | LGPL-2.1 or later |

Practical notes (engineering practice, not legal advice):

- Modifications to the FFmpeg copy inside ijkplayer (such as the custom
  protocol in `player/ffmpeg-patch/`) are kept as patches in this repository
  so the changes are visible and publishable, consistent with LGPL practice.
- If you ship a commercial product based on this stack, review the LGPL
  obligations for your distribution model (dynamic linking, providing the
  corresponding library source, etc.) with your own counsel.
- The KPLD protocol in `protocol.md` is fictional and original to this
  repository. Any resemblance to a real vendor protocol is structural only —
  frame headers, sequence numbers and timestamps are common to the genre.
