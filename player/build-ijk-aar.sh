#!/usr/bin/env bash
# Build a local AAR that bundles:
#   - ijkplayer-java classes (compiled from the ijkplayer source tree)
#   - the natively built .so libraries from the ijkplayer build tree
#     (libijkffmpeg.so contains the KDSP protocol — see ffmpeg-patch/)
#
# The stock Maven artifacts (tv.danmaku.ijk.media:ijkplayer-*:0.8.8) do
# not contain kdsp, so the demo app consumes this AAR via flatDir
# instead. The AAR is a build artifact: it is git-ignored and rebuilt
# by this script.
#
# Inputs (override via environment):
#   IJK_BUILD_DIR  ijkplayer build tree (default: ~/workspace/projects/ijkplayer-build)
#   ANDROID_JAR    android.jar to compile against
#                  (default: ~/opt/android-sdk/platforms/android-34/android.jar)
#   JAVA_HOME      JDK 17 (default: ~/opt/jdk-17)
set -euo pipefail

IJK_BUILD_DIR=${IJK_BUILD_DIR:-$HOME/workspace/projects/ijkplayer-build}
ANDROID_JAR=${ANDROID_JAR:-$HOME/opt/android-sdk/platforms/android-34/android.jar}
export JAVA_HOME=${JAVA_HOME:-$HOME/opt/jdk-17}
JAVAC="$JAVA_HOME/bin/javac"
JAR="$JAVA_HOME/bin/jar"

APP_DIR="$(cd "$(dirname "$0")" && pwd)/app"
JAVA_SRC="$IJK_BUILD_DIR/android/ijkplayer/ijkplayer-java/src/main/java"
SO_DIR="$IJK_BUILD_DIR/android/ijkplayer/ijkplayer-arm64/src/main/libs/arm64-v8a"
OUT="$APP_DIR/libs/ijkplayer-kdsp.aar"

for f in "$ANDROID_JAR" "$JAVAC" "$SO_DIR/libijkffmpeg.so" "$SO_DIR/libijkplayer.so" "$SO_DIR/libijksdl.so"; do
    [ -e "$f" ] || { echo "missing input: $f" >&2; exit 1; }
done
# sanity: the ffmpeg library must be the KDSP build, not the stock one
if ! strings "$SO_DIR/libijkffmpeg.so" | grep "ff_kdsp_protocol" > /dev/null; then
    echo "libijkffmpeg.so does not contain ff_kdsp_protocol — rebuild ijkplayer with the patch first" >&2
    exit 1
fi

WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT
mkdir -p "$WORK/classes" "$WORK/aar/jni/arm64-v8a"

echo "== compile ijkplayer-java (27 sources) =="
find "$JAVA_SRC" -name '*.java' > "$WORK/sources.txt"
"$JAVAC" -source 8 -target 8 -nowarn -cp "$ANDROID_JAR" -d "$WORK/classes" @"$WORK/sources.txt"
(cd "$WORK/classes" && "$JAR" cf "$WORK/aar/classes.jar" .)

echo "== package AAR =="
cat > "$WORK/aar/AndroidManifest.xml" << 'EOF'
<manifest xmlns:android="http://schemas.android.com/apk/res/android"
    package="tv.danmaku.ijk.media.player" />
EOF
cp "$SO_DIR"/libijkffmpeg.so "$SO_DIR"/libijkplayer.so "$SO_DIR"/libijksdl.so "$WORK/aar/jni/arm64-v8a/"
touch "$WORK/aar/R.txt"
mkdir -p "$APP_DIR/libs"
(cd "$WORK/aar" && zip -qr "$OUT" .)

echo "wrote $OUT ($(stat -c %s "$OUT") bytes)"
unzip -l "$OUT"
