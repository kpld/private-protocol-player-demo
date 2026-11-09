# Frozen build environment for the custom ijkplayer build.
#
# STATUS: template — not yet built or verified. ijkplayer k0.8.8 needs an
# old Android NDK; the exact NDK version that its build scripts accept must
# be pinned in build step 1 (see player/ijkplayer-build.md) and written
# into this file in the same commit. Do not "fix" the version by guessing.
#
# Intended usage (after pinning):
#   docker build -t kdsp-ijk-build docker/
#   docker run --rm -v "$PWD":/work -w /work kdsp-ijk-build \
#       bash player/ijkplayer-build.sh   # build script lands with step 1

FROM ubuntu:22.04

ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
        git curl unzip zip make pkg-config yasm nasm \
        openjdk-17-jdk-headless python3 perl \
        build-essential ca-certificates \
    && rm -rf /var/lib/apt/lists/*

# TODO(step 1): download and pin the verified Android NDK, e.g.
#   ENV ANDROID_NDK_HOME=/opt/android-ndk-rXXx
# TODO(step 1): pin the Android SDK command-line tools + platform for the
# demo app build (compileSdk 34 per player/app/build.gradle).

WORKDIR /work
