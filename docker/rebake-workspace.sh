#!/usr/bin/env bash
#
# Stage the Zephyr SDK (ARM-only subset) and the west workspace modules this
# project's build actually uses, so the Docker image can bake them in and run
# fully offline (no `-v` workspace mount).
#
# Produces two build contexts consumed by the Dockerfile via BuildKit named
# contexts (`sdk-src` and `ws-src`), see the Makefile `image` target:
#
#   $STAGE_DIR/sdk   -> gnu/arm-zephyr-eabi + hosttools + cmake + setup.sh ...
#   $STAGE_DIR/ws    -> .west + zephyr + the modules used by the gauge demo
#
# Usage:
#   docker/rebake-workspace.sh [STAGE_DIR]        (default: ${TMPDIR:-/tmp}/hmi-stage)
#
# Optional env:
#   ZEPHYR_SDK_INSTALL_HOST  path to the local Zephyr SDK install (default: $HOME/zephyr-sdk-1.0.1)
#   HMI_WORKSPACE            path to the west workspace root (default: auto-detected)
#
# Run this after updating zephyr / lvgl / hal_stm32 / the HMI module, then
# rebuild the image.

set -euo pipefail

STAGE_DIR=${1:-${TMPDIR:-/tmp}/hmi-stage}
SDK_DIR=${ZEPHYR_SDK_INSTALL_HOST:-${HOME}/zephyr-sdk-1.0.1}

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# Locate the west workspace root (the first directory walking up that has
# both .west/ and zephyr/ -- .west is the canonical workspace marker).
find_workspace() {
  local d
  d="${HMI_WORKSPACE:-${SCRIPT_DIR}}"
  while [ ! -d "$d/.west" ] && [ "$d" != "/" ]; do
    d="$(dirname "$d")"
  done
  if [ ! -d "$d/zephyr" ] || [ ! -d "$d/.west" ]; then
    echo "error: no west workspace found (no '.west/zephyr' directory above $SCRIPT_DIR)" >&2
    exit 1
  fi
  printf '%s' "$d"
}

WS_DIR="$(find_workspace)"

echo "SDK install : $SDK_DIR"
echo "Workspace   : $WS_DIR"
echo "Staging to  : $STAGE_DIR"
echo

# SDK version expected by the Zephyr in this workspace.
SDK_VER="$(cat "$WS_DIR/zephyr/SDK_VERSION" 2>/dev/null || true)"
[ -f "$SDK_DIR/sdk_version" ] || { echo "error: SDK dir $SDK_DIR does not look like a Zephyr SDK install" >&2; exit 1; }

# 1) Stage SDK: only the ARM target toolchain + hosttools + cmake config.
stage_sdk="${STAGE_DIR}/sdk"
stage_ws="${STAGE_DIR}/ws"
rm -rf "$STAGE_DIR"
mkdir -p "$stage_sdk/gnu" "$stage_ws"

echo "== staging SDK (ARM-only + hosttools) =="
rsync -a --exclude='.git' "$SDK_DIR/gnu/arm-zephyr-eabi" "$stage_sdk/gnu/"
rsync -a --exclude='.git' "$SDK_DIR/hosttools"          "$stage_sdk/"
for f in setup.sh sdk_version cmake sdk_gnu_toolchains; do
  [ -e "$SDK_DIR/$f" ] || { echo "error: SDK file $SDK_DIR/$f missing" >&2; exit 1; }
  rsync -a "$SDK_DIR/$f" "$stage_sdk/"
done

echo "== staging west workspace (modules used by the gauge demo) =="
rsync -a --exclude='.git' "$WS_DIR/.west"                 "$stage_ws/"
rsync -a --exclude='.git' "$WS_DIR/zephyr"                "$stage_ws/"

mkdir -p "$stage_ws/modules/hal" "$stage_ws/modules/lib/gui" "$stage_ws/modules/lib" "$stage_ws/apps"
rsync -a --exclude='.git' "$WS_DIR/modules/hal/stm32"     "$stage_ws/modules/hal/"
rsync -a --exclude='.git' "$WS_DIR/modules/hal/cmsis_6"   "$stage_ws/modules/hal/"
rsync -a --exclude='.git' "$WS_DIR/modules/lib/gui/lvgl"  "$stage_ws/modules/lib/gui/"
rsync -a --exclude='.git' "$WS_DIR/modules/lib/picolibc"  "$stage_ws/modules/lib/"
# modules/hmi is a symlink -> ../apps/HMI in the real workspace; copy the link,
# NOT its contents, then provide the target under apps/.
rsync -a "$WS_DIR/modules/hmi"                            "$stage_ws/modules/"
rsync -a --exclude='.git' "$WS_DIR/apps/HMI"              "$stage_ws/apps/"
# Optional: MCUboot (16 MB) if a signed/chainloader build is ever needed.
rsync -a --exclude='.git' "$WS_DIR/bootloader/mcuboot"    "$stage_ws/bootloader/"

echo
echo "== staged sizes =="
du -sh "$stage_sdk" "$stage_ws"
echo
echo "SDK version: $SDK_VER (workspace zephyr/SDK_VERSION)"
echo "Next: make image"