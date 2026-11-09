# Building the custom ijkplayer (arm64-v8a, Android)

> Status: **skeleton.** The exact NDK/toolchain versions below must be
> pinned during build step 1 and recorded here — ijkplayer k0.8.8 predates
> modern NDKs, and guessing versions is how these builds waste days.
> The `../docker/Dockerfile` freezes the environment once the versions are
> verified.

## Baseline

- Upstream: `https://github.com/bilibili/ijkplayer`, branch/tag **k0.8.8**
  (FFmpeg n3.4 based). Fetch it yourself; it is not vendored in this repo.
- Target: Android, **arm64-v8a only** (demo scope).

## Steps (upstream's own flow, with our patch applied)

```bash
git clone https://github.com/bilibili/ijkplayer.git
cd ijkplayer && git checkout k0.8.8

# 1. Fetch ijkplayer's FFmpeg and other deps
./init-android.sh            # or the arm64 variant the tree provides

# 2. Apply this repo's protocol patch to the FFmpeg tree
#    (paths per player/ffmpeg-patch/README.md)
cd ../ffmpeg-arm64 && patch -p1 < ../../ffmpeg-patch/0001-libavformat-add-kpld-protocol.patch
cd ..

# 3. Compile FFmpeg, then ijkplayer
cd android/contrib && ./compile-ffmpeg.sh arm64-v8a && ./compile-ffmpeg.sh build
cd .. && ./compile-ijk.sh arm64-v8a
```

> The exact script names/arguments come from the k0.8.8 tree itself; where
> they differ from the above, fix this file in the same commit as the first
> successful build. A build log of the first success belongs in `docs/`.

## Known pitfalls (recorded in advance)

1. **NDK version**: k0.8.8-era build scripts expect an old NDK. Use the
   Docker image in `../docker/` with the pinned NDK instead of your system
   NDK. Record the working NDK version here: `TODO after step 1`.
2. **16 KB page size**: stock k0.8.8 native libraries are not 16 KB-aligned.
   For a LAN demo on a current device this is usually not blocking; for
   Play-store distribution it is a separate work item (this is itself a
   billable modernisation service — see the service package notes).
3. **Host tools**: the init scripts pull several git repos; run them once
   with a good network and cache the result in the Docker image.

## Verifying the build

- The stock ijkplayer example (or `player/app/`) plays a local file.
- The same app then opens `kpld://<PC-LAN-IP>:19350` against
  `server/kpld_server.py` — that is the moment the demo exists; record a
  30-second screen capture for `docs/screenshots/` at that point.
