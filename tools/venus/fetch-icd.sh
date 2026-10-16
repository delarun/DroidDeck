#!/usr/bin/env bash
set -euo pipefail
OUTDIR=$(mkdir -p "$1" && cd "$1" && pwd)
HERE=$(cd "$(dirname "$0")" && pwd)
. "$HERE/source.env"
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT
curl -fsSL --retry 3 -o "$WORK/pkg.tar.xz" \
  "http://mirror.archlinuxarm.org/aarch64/extra/${VULKAN_VIRTIO_PACKAGE//:/%3A}"
echo "$VULKAN_VIRTIO_SHA256  $WORK/pkg.tar.xz" | shasum -a 256 -c -
tar -xJf "$WORK/pkg.tar.xz" -C "$WORK" usr/lib/libvulkan_virtio.so usr/share/licenses/vulkan-virtio/license.rst
install -Dm644 "$WORK/usr/lib/libvulkan_virtio.so" "$OUTDIR/usr/local/lib/droiddeck-venus/libvulkan_virtio.so"
install -Dm644 "$WORK/usr/share/licenses/vulkan-virtio/license.rst" "$OUTDIR/usr/local/share/licenses/vulkan-virtio/license.rst"
echo "fetched libvulkan_virtio.so (${VULKAN_VIRTIO_PACKAGE})"
