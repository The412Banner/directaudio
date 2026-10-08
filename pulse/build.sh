#!/usr/bin/env bash
# Builds the two PulseAudio 13 sinks for a host that runs the daemon on the Android side:
#   module-directaudio-native-sink.so  DirectAudio's AAudio engine in-process - the Steam client's
#                                      own sound, one step from Android (the default to offer)
#   module-directaudio-sink.so         feeds the DirectAudio relay's ring instead, so the client
#                                      shares the relay's device stream with the games ("shared
#                                      output")
# Compiled against the upstream PulseAudio 13.0 source headers and linked against the daemon
# libraries the host app ships (libpulsecore-13.0 / libpulsecommon-13.0 / libpulse, Android
# arm64), with the Android NDK (API 26 = AAudio's floor).
#
#   NDK=<ndk root> pulse/build.sh <pulseaudio-13.0 source dir> <dir with the three libpulse*.so> <output dir>
set -euo pipefail
PA_SRC=$1
LIBS=$2
OUTDIR=$3
mkdir -p "$OUTDIR"
: "${NDK:?set NDK to the Android NDK root}"
API=26
HERE=$(cd "$(dirname "$0")" && pwd)
case "$(uname -s):$(uname -m)" in
  Darwin:arm64) NDK_HOST=darwin-arm64 ;;
  Darwin:x86_64) NDK_HOST=darwin-x86_64 ;;
  Linux:x86_64) NDK_HOST=linux-x86_64 ;;
  Linux:aarch64|Linux:arm64) NDK_HOST=linux-aarch64 ;;
  *) echo "Unsupported build host: $(uname -s) $(uname -m)" >&2; exit 1 ;;
esac
TOOLCHAIN="$NDK/toolchains/llvm/prebuilt/$NDK_HOST/bin"
[[ -d "$TOOLCHAIN" ]] || TOOLCHAIN="$NDK/toolchains/llvm/prebuilt/linux-x86_64/bin"
CC="$TOOLCHAIN/aarch64-linux-android${API}-clang"
[[ -x "$CC" ]] || { echo "Android NDK toolchain not found at $TOOLCHAIN" >&2; exit 1; }
test -f "$PA_SRC/src/pulse/version.h.in"
for lib in libpulsecore-13.0.so libpulsecommon-13.0.so libpulse.so; do
  test -f "$LIBS/$lib" || { echo "missing $LIBS/$lib" >&2; exit 1; }
done
INC=$(mktemp -d)
mkdir -p "$INC/pulse"
sed -e 's/@PA_MAJOR@/13/g' -e 's/@PA_MINOR@/0/g' -e 's/@PA_API_VERSION@/12/g' -e 's/@PA_PROTOCOL_VERSION@/33/g' \
    "$PA_SRC/src/pulse/version.h.in" > "$INC/pulse/version.h"
cp "$HERE/config.h" "$HERE/ltdl.h" "$INC/"
build_one() {
  local src=$1 OUT=$2 extra=$3
  # PulseAudio 13's own headers (pulsecore/atomic.h) predate clang's int-conversion error; that one
  # is downgraded for them, the modules themselves compile clean.
  "$CC" -O2 -shared -fPIC -Wall -Wno-unused-parameter -Wno-error=int-conversion -Wno-visibility -DHAVE_CONFIG_H \
      -I"$INC" -I"$PA_SRC/src" -I"$HERE" -I"$HERE/.." \
      -o "$OUT" "$HERE/$src" \
      -L"$LIBS" -l:libpulsecore-13.0.so -l:libpulsecommon-13.0.so -l:libpulse.so $extra
  "$TOOLCHAIN/llvm-strip" --strip-unneeded "$OUT"
  # What the daemon looks for, and nothing linked that a device would not have.
  for sym in pa__init pa__done pa__get_author pa__get_description pa__get_usage pa__get_version pa__load_once; do
    "$TOOLCHAIN/llvm-nm" -D --defined-only "$OUT" | grep -q " $sym$" || { echo "ERROR: $OUT does not export $sym"; exit 1; }
  done
  local NEEDED
  NEEDED=$("$TOOLCHAIN/llvm-readelf" -d "$OUT" | sed -n 's/.*NEEDED.*\[\(.*\)\]/\1/p' | tr '\n' ' ')
  echo "$(basename "$OUT") NEEDED: $NEEDED"
  for lib in $NEEDED; do
    case $lib in libpulsecore-13.0.so|libpulsecommon-13.0.so|libpulse.so|libaaudio.so|libc.so|libm.so|libdl.so) ;;
    *) echo "ERROR: unexpected dependency $lib"; exit 1 ;;
    esac
  done
  ls -l "$OUT"
}
build_one module-directaudio-native-sink.c "$OUTDIR/module-directaudio-native-sink.so" -laaudio
build_one module-directaudio-sink.c "$OUTDIR/module-directaudio-sink.so" ""
# The native sink is the one that must carry AAudio; the relay sink must not.
"$TOOLCHAIN/llvm-readelf" -d "$OUTDIR/module-directaudio-native-sink.so" | grep -q 'libaaudio\.so' || { echo "ERROR: native sink lacks libaaudio"; exit 1; }
! "$TOOLCHAIN/llvm-readelf" -d "$OUTDIR/module-directaudio-sink.so" | grep -q 'libaaudio\.so' || { echo "ERROR: relay sink links libaaudio"; exit 1; }
