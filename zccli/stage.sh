#!/usr/bin/env bash
#
# SPDX-License-Identifier: Apache-2.0
#
# Stage the Zephyr SDK (ARM-only) and the west workspace for the zccli image,
# pruned down to what building the two supported boards actually needs.
#
# Produces two BuildKit build contexts consumed by zccli/Dockerfile:
#
#   $STAGE_DIR/sdk   -> gnu/arm-zephyr-eabi + cmake config   (no hosttools)
#   $STAGE_DIR/ws    -> .west + zephyr + modules + apps/HMI
#
# Usage:
#   zccli/stage.sh [STAGE_DIR]         (default: ${TMPDIR:-/tmp}/zccli-stage)
#
# Environment:
#   ZEPHYR_SDK_INSTALL_HOST  SDK install path (default: $HOME/zephyr-sdk-1.0.1)
#   HMI_WORKSPACE            west workspace root (default: auto-detect)
#
# Prune switches (set to 0 to re-enable a layer -- use this first if a build
# fails after an image update, so the cause can be isolated):
#   PRUNE_ZEPHYR_TREE=1   drop zephyr/{tests,doc,samples} and foreign boards
#   PRUNE_STM32CUBE=1     keep only stm32g4xx + stm32h7xx (+ common_ll)
#   PRUNE_LVGL=1          drop lvgl/{tests,examples,demos,docs}
#   PRUNE_PICOLIBC=1      drop picolibc/test
#   PRUNE_HOSTTOOLS=1     drop SDK hosttools/sysroots (~1.2 GB, Yocto loader)
#
set -euo pipefail

STAGE_DIR=${1:-${TMPDIR:-/tmp}/zccli-stage}
SDK_DIR=${ZEPHYR_SDK_INSTALL_HOST:-${HOME}/zephyr-sdk-1.0.1}

PRUNE_ZEPHYR_TREE=${PRUNE_ZEPHYR_TREE:-1}
PRUNE_STM32CUBE=${PRUNE_STM32CUBE:-1}
PRUNE_LVGL=${PRUNE_LVGL:-1}
PRUNE_PICOLIBC=${PRUNE_PICOLIBC:-1}
PRUNE_HOSTTOOLS=${PRUNE_HOSTTOOLS:-1}

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

find_workspace() {
	local d="${HMI_WORKSPACE:-$(dirname "$SCRIPT_DIR")}"
	while [ ! -d "$d/.west" ] && [ "$d" != "/" ]; do
		d="$(dirname "$d")"
	done
	[ -d "$d/zephyr" ] && [ -d "$d/.west" ] || {
		echo "error: no west workspace found above $SCRIPT_DIR" >&2
		exit 1
	}
	printf '%s' "$d"
}

WS_DIR="$(find_workspace)"

echo "SDK install : $SDK_DIR"
echo "Workspace   : $WS_DIR"
echo "Staging to  : $STAGE_DIR"
echo "Prunes      : zephyr=$PRUNE_ZEPHYR_TREE stm32cube=$PRUNE_STM32CUBE lvgl=$PRUNE_LVGL picolibc=$PRUNE_PICOLIBC hosttools=$PRUNE_HOSTTOOLS"
echo

stage_sdk="$STAGE_DIR/sdk"
stage_ws="$STAGE_DIR/ws"
rm -rf "$STAGE_DIR"
mkdir -p "$stage_sdk/gnu" "$stage_ws"

# ----------------------------------------------------------------- SDK

echo "== staging SDK (ARM-only) =="
[ -f "$SDK_DIR/sdk_version" ] || {
	echo "error: $SDK_DIR does not look like a Zephyr SDK install" >&2
	exit 1
}

# Only the cross toolchain. hosttools/ is a 1.2 GB Yocto sysroot whose ELF
# binaries carry a PT_INTERP pointing into it; Zephyr only appends it to
# CMAKE_PREFIX_PATH (cmake/zephyr/host-tools.cmake), so a system dtc/ninja/
# cmake from apt replaces it. Dropping it also removes the need for the
# /home/<user> symlink hack that patched PT_INTERP used to force.
rsync -a --exclude='.git' "$SDK_DIR/gnu/arm-zephyr-eabi" "$stage_sdk/gnu/"
for f in setup.sh sdk_version cmake sdk_gnu_toolchains; do
	[ -e "$SDK_DIR/$f" ] || { echo "error: $SDK_DIR/$f missing" >&2; exit 1; }
	rsync -a "$SDK_DIR/$f" "$stage_sdk/"
done
if [ "$PRUNE_HOSTTOOLS" = "1" ]; then
	echo "  skipping hosttools (re-enable with PRUNE_HOSTTOOLS=0)"
else
	rsync -a --exclude='.git' "$SDK_DIR/hosttools" "$stage_sdk/"
fi

# --------------------------------------------------------------- ZEPHYR

echo "== staging workspace =="
# rsync's default is to copy everything; every --exclude below is anchored
# with a leading slash so it only matches at the top level of its own copy.
rsync -a --exclude='.git' --exclude='.cache' "$WS_DIR/.west" "$stage_ws/"

# NOTE ON TRAILING SLASHES: a pattern with a leading `/` is anchored to the
# root of the transfer, and that root is the source argument as written.
# Copying "$WS_DIR/zephyr" (no slash) makes the root the PARENT, so /tests
# would be matched against "zephyr/tests" and never hit. Every pruned copy
# below therefore uses a trailing slash plus an explicit destination, which
# makes /tests mean exactly zephyr/tests.
zephyr_filters=()
if [ "$PRUNE_ZEPHYR_TREE" = "1" ]; then
	zephyr_filters+=(
		# Not consulted by `west build`: twister reads tests/, doc/ is only
		# for building the docs.
		'--exclude=/tests'
		'--exclude=/doc'
		'--exclude=/samples'
		# boards/CMakeLists.txt only add_subdirectory()'s the selected board
		# plus shields/, so nothing enumerates the vendor directories.
		#
		# boards/Kconfig, however, does ONE unconditional vendor reference:
		#   rsource "espressif/common/Kconfig.qemu"   (boards/Kconfig:189)
		# and boards/Kconfig.v2 does
		#   osource "$(KCONFIG_BOARD_DIR)/Kconfig.$(BOARD)"
		# so Kconfig fragments MUST be kept even for vendors we never build.
		# Keeping every Kconfig* under boards/ (2388 files, 9.4 MB) covers
		# both the selected board and any vendor fragment a top-level Kconfig
		# names, without having to enumerate them -- a future Zephyr release
		# cannot add an rsource that breaks this. Rule order matters: rsync
		# takes the first match, so the includes must precede /boards/**.
		'--include=/boards/'
		'--include=/boards/**/'         # descend into every vendor dir
		'--include=Kconfig*'            # keep Kconfig fragments at any depth
		'--include=/boards/CMakeLists.txt'
		'--include=/boards/deprecated.cmake'
		'--include=/boards/common/***'
		'--include=/boards/shields/***'
		'--include=/boards/st/***'
		'--exclude=/boards/**'          # everything else under boards/
	)
fi
rsync -a --exclude='.git' --exclude='.cache' "${zephyr_filters[@]+"${zephyr_filters[@]}"}" \
	"$WS_DIR/zephyr/" "$stage_ws/zephyr/"

# --------------------------------------------------------------- MODULES

mkdir -p "$stage_ws/modules/hal" "$stage_ws/modules/lib/gui" "$stage_ws/modules/lib" "$stage_ws/apps"

echo "  modules/hal/stm32"
stm32_filters=()
if [ "$PRUNE_STM32CUBE" = "1" ]; then
	# stm32cube/CMakeLists.txt does a single add_subdirectory() for the
	# selected series (series_list.cmake is only a name list), and no
	# Kconfig sources into these directories -- so everything but the two
	# supported families, the top-level files and common_ll can go.
	stm32_filters+=(
		'--include=/stm32cube/'
		'--include=/stm32cube/CMakeLists.txt'
		'--include=/stm32cube/series_list.cmake'
		'--include=/stm32cube/common_ll/***'
		'--include=/stm32cube/stm32g4xx/***'
		'--include=/stm32cube/stm32h7xx/***'
		'--exclude=/stm32cube/*'
	)
fi
rsync -a --exclude='.git' "${stm32_filters[@]+"${stm32_filters[@]}"}" \
	"$WS_DIR/modules/hal/stm32/" "$stage_ws/modules/hal/stm32/"

rsync -a --exclude='.git' "$WS_DIR/modules/hal/cmsis_6" "$stage_ws/modules/hal/"

echo "  modules/lib/gui/lvgl"
lvgl_filters=()
if [ "$PRUNE_LVGL" = "1" ]; then
	# lvgl's main.cmake globs examples/ and demos/ and degrades gracefully
	# when they are absent ("No Examples found. Disabling Examples build"),
	# and nothing in the build reads tests/ or docs/.
	# env_support/ and scripts/ are kept: main.cmake and kconfig.cmake
	# both include files from them.
	lvgl_filters+=(
		'--exclude=/tests'
		'--exclude=/examples'
		'--exclude=/demos'
		'--exclude=/docs'
	)
fi
rsync -a --exclude='.git' "${lvgl_filters[@]+"${lvgl_filters[@]}"}" \
	"$WS_DIR/modules/lib/gui/lvgl/" "$stage_ws/modules/lib/gui/lvgl/"

echo "  modules/lib/picolibc"
picolibc_filters=()
[ "$PRUNE_PICOLIBC" = "1" ] && picolibc_filters+=('--exclude=/test')
rsync -a --exclude='.git' "${picolibc_filters[@]+"${picolibc_filters[@]}"}" \
	"$WS_DIR/modules/lib/picolibc/" "$stage_ws/modules/lib/picolibc/"

# modules/hmi is a symlink -> ../apps/HMI in the real workspace. Copy the
# link itself (no trailing slash on the source arg keeps it a symlink); its
# target is staged separately under apps/ below, and the link resolves
# correctly in-container because ../apps/HMI still lands on apps/HMI.
rsync -a "$WS_DIR/modules/hmi" "$stage_ws/modules/"

# Copy the link's target. Never bake build dirs
# or a host venv -- their CMakeCache.txt carries absolute host paths that
# break `west build -p always` inside the container.
#
# 'build' and 'stm32cubeprog-tree.tar.gz' contain no '/', so rsync matches
# them at any depth (e.g. samples/svpwm_comm/build).
# The tarball is excluded because zccli flashes from the host and the image
# ships no STM32CubeProgrammer at all.
rsync -a --exclude='.git' --exclude='.cache' --exclude='build' --exclude='build-*' \
	--exclude='.venv' --exclude='__pycache__' \
	--exclude='stm32cubeprog-tree.tar.gz' \
	--exclude='/zccli/app/src/user' \
	"$WS_DIR/apps/HMI/" "$stage_ws/apps/HMI/"

echo
echo "== staged sizes =="
du -sh "$stage_sdk" "$stage_ws" 2>/dev/null || true
du -sh "$stage_sdk"/* "$stage_ws"/* 2>/dev/null | sort -h || true
echo
echo "Next: zccli/docker-build.sh"  # or: make zccli-image
