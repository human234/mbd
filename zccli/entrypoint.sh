#!/usr/bin/env bash
#
# SPDX-License-Identifier: Apache-2.0
#
# zccli command dispatcher. Runs INSIDE the container, normally via
# `docker exec -w /src zccli /entrypoint.sh ...` from zccli.ps1.
#
# Everything Zephyr-specific lives here so the user's only view of it is the
# command names below.
#
#   build [FILE.c ...] [--board <board>] [--clean]
#   export
#   size
#   clean
#   doctor
#   shell
#   help
#
set -euo pipefail

APP="${ZC_APP:-/workdir/apps/HMI/zccli/app}"
WORKDIR="${ZC_WORKDIR:-/workdir}"
BUILD_ROOT="${ZC_BUILD_ROOT:-/build}"
SRC="${ZC_SRC:-/src}"
OUT="${ZC_OUT:-$SRC/out}"
DEFAULT_BOARD="${ZC_BOARD:-nucleo_h743zi}"

# Basenames staged into the template by stage_user_files(); see below.
STAGED_FILES=()

export ZEPHYR_TOOLCHAIN_VARIANT="${ZEPHYR_TOOLCHAIN_VARIANT:-zephyr}"
export ZEPHYR_SDK_INSTALL_DIR="${ZEPHYR_SDK_INSTALL_DIR:-/opt/toolchains/zephyr-sdk-1.0.1}"

# The image's Python venv carries west + Zephyr's build requirements.
if [ -d /opt/venv/bin ]; then
	PATH="/opt/venv/bin:$PATH"
fi

log() { printf '%s\n' "$*"; }
err() { printf 'zccli: %s\n' "$*" >&2; }

# Scratch log for a build; cleaned up on any exit path (the container
# outlives `docker exec`, so leftovers in /tmp would accumulate).
TMPLOG=""
cleanup() {
	[ -n "$TMPLOG" ] && rm -f "$TMPLOG"
	return 0
}
trap cleanup EXIT

usage() {
	cat <<'EOF'
zccli -- builds and flashes your screen without touching Zephyr.

  zccli build [FILE.c ...] [--board <board>] [--clean]
      Copy your .c file(s) into the hidden template and build them.
      With no file, every top-level .c in the project folder is used.
      Default board: nucleo_h743zi (override with --board).

  zccli export          Copy the finished firmware into ./out/
  zccli size            Show flash/RAM usage for the last build
  zccli clean [--board] Delete the cached build for that board
  zccli doctor          Check the toolchain, mounts and build cache
  zccli shell           Open a shell inside the container

Your .c file should define:

  #include <lvgl.h>
  #include <hmi/hmi.h>
  #include "zccli.h"

  void zc_user_init(lv_obj_t *root) {
      /* build your screen here */
  }

Full LVGL and the HMI widgets are both available inside it.
EOF
}

die() {
	err "$*"
	exit 1
}

# ---------------------------------------------------------------- helpers

build_dir_for() {
	printf '%s/%s' "$BUILD_ROOT" "$1"
}

require_app() {
	[ -f "$APP/CMakeLists.txt" ] || die "template app missing at $APP"
	[ -f "$APP/prj.conf" ] || die "template prj.conf missing at $APP"
}

require_build_root() {
	mkdir -p "$BUILD_ROOT" 2>/dev/null || true
	[ -d "$BUILD_ROOT" ] || die "$BUILD_ROOT does not exist"

	# A Windows bind mount here would make the ~20k-file Zephyr build crawl.
	local fstype
	fstype="$(findmnt -no FSTYPE --target "$BUILD_ROOT" 2>/dev/null || true)"
	case "$fstype" in
	9p | virtiofs | fuse | fusectl)
		err "$BUILD_ROOT is on a '$fstype' mount (a Windows bind mount)."
		err "The build directory must be a named volume, not a shared folder:"
		err "  docker run ... -v zccli-build:/build ..."
		exit 1
		;;
	esac
}

# Resolve a user-supplied path and make sure it is inside the mounted source
# folder. Returns the absolute path on stdout.
resolve_src() {
	local raw="$1" abs
	case "$raw" in
	/*) abs="$raw" ;;
	*) abs="$PWD/$raw" ;;
	esac

	# Normalise ../ and ./ without requiring the file to exist yet.
	abs="$(realpath -ms "$abs")"
	case "$abs" in
	"$SRC"/*) ;;
	*)
		die "'$raw' is outside the mounted project folder ($SRC).
       Run zccli from the folder you mounted, or mount its parent."
		;;
	esac
	printf '%s' "$abs"
}

# Print the FLASH/RAM summary lines out of a captured west build log.
# When a build dir is given as the second argument the same lines are saved
# to <build>/zccli-size.txt, so `zccli doctor` can report them later without
# having to run `west build` again (which would reconfigure the template with
# an empty src/user/ and relink placeholder firmware over the real artifacts).
report_usage() {
	local logf="$1" bdir="${2:-}" line

	if [ -n "$bdir" ] && grep -q 'FLASH:' "$logf" 2>/dev/null; then
		grep -E 'FLASH:|RAM:' "$logf" >"$bdir/zccli-size.txt" 2>/dev/null || true
	fi

	if grep -q 'FLASH:' "$logf" 2>/dev/null; then
		log ""
		log "  memory"
		while IFS= read -r line; do
			case "$line" in
			*FLASH:* | *RAM:*) printf '    %s\n' "$line" ;;
			esac
		done <"$logf"
	fi
}

# ---------------------------------------------------------------- staging

# Discover (or accept) the project's .c files and copy them into the hidden
# template. Fills the global STAGED_FILES with the basenames staged.
#
# EVERY command that runs `west build` must call this first. The template
# lives in /workdir, which is baked into the image and is NOT mounted, so a
# fresh `docker run --rm` container always starts with an empty src/user/.
# Building without staging would silently relink a placeholder firmware
# into the shared build cache and overwrite the real artifacts.
stage_user_files() {
	local -a files=("$@")
	local -a found=()
	local f src dst base

	# No file named: take every top-level .c in the project folder.
	if [ "${#files[@]}" -eq 0 ]; then
		while IFS= read -r f; do
			found+=("$f")
		done < <(find "$SRC" -maxdepth 1 -type f -name '*.c' 2>/dev/null | sort)

		if [ "${#found[@]}" -eq 0 ]; then
			die "no .c file given and none found in $SRC.
       usage: zccli build my_screen.c"
		fi
		if [ "${#found[@]}" -gt 8 ]; then
			die "found ${#found[@]} .c files in $SRC; name the ones to build."
		fi
		files=("${found[@]}")
	fi

	# Clear previous copies, then copy each file in with CR stripped.
	# Only src/user/ is written, so the frozen runtime and config stay intact.
	mkdir -p "$APP/src/user"
	find "$APP/src/user" -maxdepth 1 -type f -name '*.c' -delete

	STAGED_FILES=()
	for f in "${files[@]}"; do
		src="$(resolve_src "$f")"
		[ -f "$src" ] || die "no such file: $f"
		base="$(basename "$src")"
		case "$base" in
		zc_core.c) die "'zc_core.c' is reserved by the runtime" ;;
		esac

		dst="$APP/src/user/$base"
		if [ -e "$dst" ]; then
			die "two inputs are both named '$base'; rename one."
		fi
		# sed strips a trailing CR (CRLF endings from Windows editors).
		# It only matches at end-of-line, so CR bytes inside literals survive.
		sed 's/\r$//' "$src" >"$dst"
		STAGED_FILES+=("$base")
	done
}

# ---------------------------------------------------------------- build

cmd_build() {
	local board="$DEFAULT_BOARD"
	local clean=0
	local -a files=()

	while [ $# -gt 0 ]; do
		case "$1" in
		--board)
			[ $# -ge 2 ] || die "--board needs a value"
			board="$2"
			shift 2
			;;
		--board=*)
			board="${1#--board=}"
			shift
			;;
		--clean | -c)
			clean=1
			shift
			;;
		-h | --help)
			log "usage: zccli build [FILE.c ...] [--board <board>] [--clean]"
			return 0
			;;
		--)
			shift
			break
			;;
		-*)
			die "unknown build option '$1' (try: zccli help)"
			;;
		*)
			files+=("$1")
			shift
			;;
		esac
	done

	require_app
	require_build_root
	stage_user_files "${files[@]+"${files[@]}"}"

	log "zccli: board    $board"
	log "zccli: sources  ${STAGED_FILES[*]}"

	local bdir pargs=()
	bdir="$(build_dir_for "$board")"
	[ "$clean" -eq 1 ] && pargs+=(-p always)

	TMPLOG="$(mktemp)"

	cd "$WORKDIR"
	log ""
	if ! west build -b "$board" -d "$bdir" "${pargs[@]+"${pargs[@]}"}" "$APP" 2>&1 | tee "$TMPLOG"; then
		log ""
		err "BUILD FAILED -- see the compiler error above."
		err "The first error usually points at the real problem:"
		grep -E -m 3 'error:|Error [0-9]' "$TMPLOG" >&2 || true
		exit 1
	fi

	report_usage "$TMPLOG" "$bdir"
	log ""
	cmd_export "$board"
}

# --------------------------------------------------------------- export

cmd_export() {
	local board="${1:-$DEFAULT_BOARD}"
	local bdir srcdir

	bdir="$(build_dir_for "$board")"
	srcdir="$bdir/zephyr"
	[ -d "$srcdir" ] || die "no build for board '$board' yet (run: zccli build)"

	mkdir -p "$OUT"
	local f n=0
	for f in zephyr.hex zephyr.bin zephyr.elf zephyr.map; do
		if [ -f "$srcdir/$f" ]; then
			cp -f "$srcdir/$f" "$OUT/$f"
			n=$((n + 1))
		fi
	done
	[ "$n" -gt 0 ] || die "no artifacts found in $srcdir"

	log "zccli: wrote $n file(s) to $OUT"
	local hex="$OUT/zephyr.hex"
	[ -f "$hex" ] && log "zccli: firmware $hex ($(wc -c <"$hex") bytes)"
}

# ----------------------------------------------------------------- size

cmd_size() {
	local board="${1:-$DEFAULT_BOARD}"
	local bdir outlog

	require_app
	require_build_root
	bdir="$(build_dir_for "$board")"
	[ -d "$bdir" ] || die "no build for board '$board' yet (run: zccli build)"

	# Stage first, exactly as `build` does: a fresh container has an empty
	# template, and reconfiguring without the project's sources would relink
	# a placeholder firmware over the real artifacts in the shared cache.
	stage_user_files

	# The FLASH/RAM table is printed by the final link step. On a no-op build
	# west skips linking entirely, which made `zccli size` print nothing at
	# all after a successful build -- so drop the elf to force the link to run.
	rm -f "$bdir/zephyr/zephyr.elf"

	outlog="$(mktemp)"
	TMPLOG="$outlog"

	cd "$WORKDIR"
	if ! west build -b "$board" -d "$bdir" 2>&1 | tee "$outlog" >/dev/null; then
		log ""
		err "size: build failed -- the last errors are below."
		tail -n 40 "$outlog" >&2 || true
		exit 1
	fi
	report_usage "$outlog" "$bdir"
	log ""
}

# ---------------------------------------------------------------- clean

cmd_clean() {
	local board="${1:-$DEFAULT_BOARD}"
	local bdir
	bdir="$(build_dir_for "$board")"
	if [ -d "$bdir" ]; then
		rm -rf "$bdir"
		log "zccli: removed $bdir"
	else
		log "zccli: nothing to clean for '$board'"
	fi
}

# --------------------------------------------------------------- doctor

cmd_doctor() {
	local ok=0 warn=0 fail=0

	check() { # check <label> <command...>
		local label="$1"
		shift
		if "$@" >/dev/null 2>&1; then
			log "  ok    $label"
			ok=$((ok + 1))
		else
			log "  FAIL  $label"
			fail=$((fail + 1))
		fi
	}

	warn_check() {
		local label="$1"
		shift
		if "$@" >/dev/null 2>&1; then
			log "  ok    $label"
			ok=$((ok + 1))
		else
			log "  warn  $label"
			warn=$((warn + 1))
		fi
	}

	log ""
	log "zccli doctor -- inside the container"
	log ""

	check "ARM toolchain ($ZEPHYR_SDK_INSTALL_DIR)" \
		"$ZEPHYR_SDK_INSTALL_DIR/gnu/arm-zephyr-eabi/bin/arm-zephyr-eabi-gcc" --version
	check "west build tool" command -v west
	check "template app ($APP)" test -f "$APP/CMakeLists.txt"
	check "prj.conf" test -f "$APP/prj.conf"
	check "HMI module (Kconfig)" test -f "$APP/../../Kconfig"
	check "board overlay for $DEFAULT_BOARD" test -f "$APP/boards/$DEFAULT_BOARD.overlay"

	# `-d "$SRC"` alone is not enough: the image creates /src, so it is
	# always present and writable even with no -v mount at all. findmnt
	# reports "/" for the container rootfs and "/src" only for a real bind
	# mount, which is the case that actually matters.
	local srcmnt
	srcmnt="$(findmnt -no TARGET --target "$SRC" 2>/dev/null || printf '%s' /)"
	if [ "$srcmnt" = "$SRC" ]; then
		log "  ok    project folder mounted at $SRC"
		ok=$((ok + 1))
	elif [ ! -d "$SRC" ] || [ ! -w "$SRC" ]; then
		log "  FAIL  project folder not writable at $SRC"
		fail=$((fail + 1))
	else
		log "  FAIL  no project folder mounted at $SRC"
		log "        the container sees an empty folder, so 'zccli build' will"
		log "        find no .c files. Start zccli from your project folder."
		fail=$((fail + 1))
	fi

	if [ -d "$BUILD_ROOT" ]; then
		local fstype
		fstype="$(findmnt -no FSTYPE --target "$BUILD_ROOT" 2>/dev/null || echo unknown)"
		case "$fstype" in
		9p | virtiofs | fuse)
			log "  FAIL  build dir is on '$fstype' (Windows bind mount) -- builds will be slow"
			fail=$((fail + 1))
			;;
		*)
			log "  ok    build cache at $BUILD_ROOT ($fstype)"
			ok=$((ok + 1))
			;;
		esac
	else
		log "  warn  build cache $BUILD_ROOT missing"
		warn=$((warn + 1))
	fi

	# Last build's memory usage, as recorded by the most recent build/size.
	# Deliberately NOT re-running `west build` here: doctor runs in a fresh
	# container whose template has no user sources, so a build would reconfigure
	# the app and relink placeholder firmware over the real artifacts.
	local bdir sizefile line
	bdir="$(build_dir_for "$DEFAULT_BOARD")"
	sizefile="$bdir/zccli-size.txt"
	if [ -f "$sizefile" ]; then
		log ""
		log "  last build ($DEFAULT_BOARD):"
		log "  memory"
		while IFS= read -r line; do
			printf '    %s\n' "$line"
		done <"$sizefile"
	fi

	log ""
	log "  $ok ok, $warn warning(s), $fail failure(s)"
	log ""
	log "  flash the board from your Windows machine:"
	log "    zccli.ps1 flash"
	log ""
	[ "$fail" -eq 0 ]
}

# ---------------------------------------------------------------- shell

cmd_shell() {
	log "zccli: opening a shell in the container (cwd: $PWD)"
	log "zccli: the build runs with: cd $WORKDIR && west build ..."
	log ""
	exec bash -l
}

# ----------------------------------------------------------------- main

cmd="${1:-help}"
[ $# -gt 0 ] && shift || true

case "$cmd" in
build) cmd_build "$@" ;;
export) cmd_export "$@" ;;
size) cmd_size "$@" ;;
clean) cmd_clean "$@" ;;
doctor) cmd_doctor "$@" ;;
shell | bash) cmd_shell "$@" ;;
help | -h | --help) usage ;;
*)
	err "unknown command '$cmd'"
	usage
	exit 2
	;;
esac
