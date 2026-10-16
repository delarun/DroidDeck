#!/usr/bin/env bash
set -euo pipefail
OUTDIR=$(mkdir -p "$1" && cd "$1" && pwd)
: "${NDK:?set NDK to the Android NDK root}"
API=31
HERE=$(cd "$(dirname "$0")" && pwd)
. "$HERE/source.env"
case "$(uname -s):$(uname -m)" in
  Darwin:*) NDK_HOST=darwin-x86_64 ;;
  Linux:x86_64) NDK_HOST=linux-x86_64 ;;
  Linux:aarch64|Linux:arm64) NDK_HOST=linux-aarch64 ;;
  *) echo "Unsupported build host: $(uname -s) $(uname -m)" >&2; exit 1 ;;
esac
TOOLCHAIN="$NDK/toolchains/llvm/prebuilt/$NDK_HOST/bin"
if [[ ! -d "$TOOLCHAIN" && -d "$NDK/toolchains/llvm/prebuilt/linux-x86_64/bin" ]]; then
  TOOLCHAIN="$NDK/toolchains/llvm/prebuilt/linux-x86_64/bin"
fi
for tool in meson ninja; do
  command -v "$tool" >/dev/null || { echo "$tool is required to build the Venus server" >&2; exit 1; }
done
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT
curl -fsSL --retry 3 -o "$WORK/virgl.tar.gz" \
  "https://gitlab.freedesktop.org/virgl/virglrenderer/-/archive/${VIRGLRENDERER_COMMIT}/virglrenderer-${VIRGLRENDERER_COMMIT}.tar.gz"
echo "$VIRGLRENDERER_SHA256  $WORK/virgl.tar.gz" | shasum -a 256 -c -
mkdir -p "$WORK/src"
tar -xzf "$WORK/virgl.tar.gz" -C "$WORK/src" --strip-components=1
for patch in "$HERE"/patches/*.patch; do
  patch -d "$WORK/src" -p1 --forward --quiet < "$patch"
done
cat > "$WORK/cross.ini" <<EOF
[binaries]
c = '$TOOLCHAIN/aarch64-linux-android${API}-clang'
cpp = '$TOOLCHAIN/aarch64-linux-android${API}-clang++'
ar = '$TOOLCHAIN/llvm-ar'
strip = '$TOOLCHAIN/llvm-strip'
pkg-config = 'false'

[built-in options]
c_args = ['-I$HERE/compat', '-ffile-prefix-map=$WORK=.']
c_link_args = ['-llog']

[host_machine]
system = 'android'
cpu_family = 'aarch64'
cpu = 'armv8'
endian = 'little'
EOF
meson setup "$WORK/build" "$WORK/src" --cross-file "$WORK/cross.ini" --buildtype=release --default-library=static \
  -Dvrend=false -Dvenus=true -Dvulkan-dload=true -Dplatforms=[] \
  -Drender-server-mode=thread -Drender-server-worker=thread >/dev/null
ninja -C "$WORK/build" vtest/virgl_test_server >/dev/null
"$TOOLCHAIN/llvm-strip" -o "$OUTDIR/libvenus-server.so" "$WORK/build/vtest/virgl_test_server"
echo "built $OUTDIR/libvenus-server.so (virglrenderer ${VIRGLRENDERER_COMMIT:0:12})"
