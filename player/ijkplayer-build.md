# Building the custom ijkplayer (arm64-v8a, Android)

> Status: **verified end-to-end (2026-11-09).** The native libraries and
> the demo APK were built with exactly the versions below; the APK's
> `libijkffmpeg.so` exports `ff_kdsp_protocol`.

## Pinned toolchain

| Component | Version | Notes |
|---|---|---|
| JDK | OpenJDK 17 (Amazon Corretto 17.0.21) | runs both the NDK-era scripts and Gradle |
| Android NDK | **r10e-rc4 (64-bit)** | the version ijkplayer k0.8.8's README prescribes |
| ijkplayer | k0.8.8, FFmpeg from Bilibili/FFmpeg `ff3.4--ijk0.8.7--20180103--001` | fetched by `init-android.sh` |
| Android SDK | platform 34, build-tools 34.0.0 | for the demo app only |
| Gradle / AGP | Gradle 8.7 / AGP 8.5.2 | the app does **not** use ijkplayer's Gradle 2.x build |

## Native build (upstream's own flow, with our patch applied)

```bash
git clone https://github.com/bilibili/ijkplayer.git
cd ijkplayer && git checkout k0.8.8
export ANDROID_NDK=<path to android-ndk-r10e>

./init-android.sh

# Apply the KDSP patch to the per-arch FFmpeg copy
cd android/contrib/ffmpeg-arm64
patch -p1 < <this-repo>/player/ffmpeg-patch/0001-libavformat-add-kdsp-protocol.patch
cd ../..

# Compile FFmpeg, then ijkplayer (argument is "arm64", not "arm64-v8a")
cd android/contrib && ./compile-ffmpeg.sh arm64
cd .. && ./compile-ijk.sh arm64
# -> android/ijkplayer/ijkplayer-arm64/src/main/libs/arm64-v8a/*.so
```

Pitfalls actually hit (all recorded in `docs/build-companion.md`):

1. **NDK r10e host detection**: `make-standalone-toolchain.sh` misdetects
   some 64-bit Linux hosts as `linux-x86`. Pre-generate the standalone
   toolchain with `--system=linux-x86_64` at the path ijkplayer expects
   (`android/contrib/build/ffmpeg-arm64/toolchain`) and `touch` its
   marker file so ijkplayer skips its own invocation.
2. **Stale-configure shortcut**: if `ffmpeg-arm64/config.h` exists, the
   compile script skips configure entirely and a newly added protocol is
   silently missing from the output. Delete `config.h` after applying
   the patch, before compiling.
3. **16 KB page size**: stock k0.8.8 native libraries are not 16 KB-aligned.
   For a LAN demo on a current device this is usually not blocking; for
   Play-store distribution it is a separate work item.

## Demo app build

The app does not use ijkplayer's own Gradle build. Instead:

1. `./build-ijk-aar.sh` compiles the `ijkplayer-java` sources against
   `android.jar` and packages them with the three native libraries into
   `app/libs/ijkplayer-kdsp.aar` (a build artifact, git-ignored). The
   script refuses to package a `libijkffmpeg.so` that lacks
   `ff_kdsp_protocol`.
2. `gradle assembleDebug` (Gradle 8.7, AGP 8.5.2, compileSdk 34) builds
   `app-debug.apk` (~2 MB, package `demo.kdsp.player`, arm64-v8a only).

Restricted-network note: if the JVM cannot reach Maven repositories
(some proxied environments break Java's HTTP stack while curl works),
pre-fetch the artifacts listed by Gradle's offline errors into a
`file://` Maven repository with curl and build with `--offline` plus an
init script that puts that repository first. AGP and all of its
dependencies are available from Google Maven / Google's Maven Central
mirror, so no other hosts are required.

## Verifying the build

- `strings` / `nm -D` on the APK's `libijkffmpeg.so` shows
  `ff_kdsp_protocol`, `kdsp_average_speed`, `kdsp_realtime_speed`.
- On a device: install the APK, run `server/kdsp_server.py` on a PC on
  the same LAN, and open `kdsp://<PC-LAN-IP>:19350` in the app. Record a
  screen capture for `docs/screenshots/` at that point.
