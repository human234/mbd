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
#   sample [NAME] [--board <board>] [--clean]
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

# Bundled demo apps that `zccli sample` can build instead of the template.
SAMPLES_DIR="${ZC_SAMPLES_DIR:-$WORKDIR/apps/HMI/samples}"

# Which app the last build used, so export/size/clean/doctor keep working
# after a `sample` build without the user having to repeat the name.
LAST_APP_FILE="$BUILD_ROOT/last-app"

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

  zccli sample [NAME] [--board <board>] [--clean]
      Build one of the bundled demo apps instead of your own .c file(s).
      With no NAME, list what is available. export/size/clean/flash all
      follow the last build, so they work unchanged afterwards.

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

build_dir_for() { # build_dir_for <board> [sample-name]
	local board="$1" name="${2:-}"
	if [ -n "$name" ]; then
		printf '%s/sample-%s/%s' "$BUILD_ROOT" "$name" "$board"
	else
		printf '%s/%s' "$BUILD_ROOT" "$board"
	fi
}

# Record which app the last successful build used, as three fields:
#   template - <board>
#   sample <name> <board>
# export/size/clean/doctor read it back so the user never has to repeat the
# sample name, and so a sample build can never overwrite the template's cache.
write_last() { # write_last <kind> <sample-name-or--> <board>
	mkdir -p "$BUILD_ROOT" 2>/dev/null || true
	printf '%s %s %s\n' "$1" "${2:--}" "$3" >"$LAST_APP_FILE"
}

# Fill TARGET_KIND / TARGET_NAME / TARGET_BOARD / TARGET_BDIR from the
# recorded last build. An optional argument overrides the board.
# No state file means "template", which is exactly what caches built before
# `sample` existed hold.
resolve_last() { # resolve_last [board-override]
	local kind="template" name="" board="$DEFAULT_BOARD"
	local k n b

	if [ -f "$LAST_APP_FILE" ]; then
		read -r k n b <"$LAST_APP_FILE" || true
		case "${k:-}" in
		template | sample)
			kind="$k"
			if [ "${n:--}" != "-" ]; then
				name="$n"
			fi
			if [ -n "${b:-}" ]; then
				board="$b"
			fi
			;;
		esac
	fi

	if [ -n "${1:-}" ]; then
		board="$1"
	fi

	TARGET_KIND="$kind"
	TARGET_NAME="$name"
	TARGET_BOARD="$board"
	TARGET_BDIR="$(build_dir_for "$board" "$name")"
}

# Every demo app shipped in the image that has a CMakeLists.txt.
list_samples() {
	local d
	if [ -d "$SAMPLES_DIR" ]; then
		for d in "$SAMPLES_DIR"/*; do
			if [ -f "$d/CMakeLists.txt" ]; then
				basename "$d"
			fi
		done
	fi
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

	write_last template - "$board"
	report_usage "$TMPLOG" "$bdir"
	log ""
	cmd_export "$board"
}

# ---------------------------------------------------------------- sample

# Build one of the demo apps bundled in the image instead of the user's own
# .c files. Same shape as cmd_build, minus stage_user_files/require_app: the
# samples carry their own sources and prj.conf, and staging the template would
# be pointless (and would leave stale copies behind for the next `build`).
cmd_sample() {
	local board="$DEFAULT_BOARD"
	local clean=0
	local name=""

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
			log "usage: zccli sample [NAME] [--board <board>] [--clean]"
			return 0
			;;
		--)
			shift
			break
			;;
		-*)
			die "unknown sample option '$1' (try: zccli help)"
			;;
		*)
			if [ -n "$name" ]; then
				die "sample takes at most one name (got '$name' and '$1')"
			fi
			name="$1"
			shift
			;;
		esac
	done

	require_build_root

	local -a available=()
	local s
	while IFS= read -r s; do
		available+=("$s")
	done < <(list_samples)

	if [ "${#available[@]}" -eq 0 ]; then
		die "no sample apps are bundled in this image."
	fi

	# Bare `zccli sample`: show the menu rather than guessing.
	if [ -z "$name" ]; then
		log "zccli: bundled samples -- build one with:  zccli sample <name>"
		log ""
		for s in "${available[@]}"; do
			log "  $s"
		done
		log ""
		log "  add --board to override $DEFAULT_BOARD"
		return 0
	fi

	local known=""
	for s in "${available[@]}"; do
		if [ "$s" = "$name" ]; then
			known=1
		fi
	done
	if [ -z "$known" ]; then
		die "no sample called '$name'.
       available: ${available[*]}"
	fi

	local sdir="$SAMPLES_DIR/$name"
	[ -f "$sdir/CMakeLists.txt" ] || die "sample '$name' is incomplete (no CMakeLists.txt)"
	[ -f "$sdir/prj.conf" ] || die "sample '$name' is incomplete (no prj.conf)"

	# motor_hmi only ships an H743 overlay; say so before west fails obscurely.
	if [ -d "$sdir/boards" ] &&
		[ ! -f "$sdir/boards/$board.overlay" ] &&
		[ ! -f "$sdir/boards/$board.conf" ]; then
		err "note: sample '$name' ships no '$board' overlay; it may build without a display"
	fi

	log "zccli: sample   $name"
	log "zccli: board    $board"

	local bdir pargs=()
	bdir="$(build_dir_for "$board" "$name")"
	[ "$clean" -eq 1 ] && pargs+=(-p always)

	TMPLOG="$(mktemp)"

	cd "$WORKDIR"
	log ""
	if ! west build -b "$board" -d "$bdir" "${pargs[@]+"${pargs[@]}"}" "$sdir" 2>&1 | tee "$TMPLOG"; then
		log ""
		err "BUILD FAILED -- see the compiler error above."
		err "The first error usually points at the real problem:"
		grep -E -m 3 'error:|Error [0-9]' "$TMPLOG" >&2 || true
		exit 1
	fi

	write_last sample "$name" "$board"
	report_usage "$TMPLOG" "$bdir"
	log ""
	cmd_export "$board"
}

# --------------------------------------------------------------- export

cmd_export() {
	resolve_last "${1:-}"
	local srcdir="$TARGET_BDIR/zephyr"

	[ -d "$srcdir" ] || die "no build for board '$TARGET_BOARD' yet (run: zccli build or zccli sample)"

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
	resolve_last "${1:-}"
	local bdir="$TARGET_BDIR"
	local board="$TARGET_BOARD"
	local outlog

	require_build_root
	[ -d "$bdir" ] || die "no build for board '$board' yet (run: zccli build or zccli sample)"

	# Only the template needs its sources staged. A sample build is
	# self-contained: staging here would copy the project's .c files into a
	# template that this command is not going to build.
	if [ "$TARGET_KIND" = "template" ]; then
		require_app
		stage_user_files
	fi

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
	resolve_last "${1:-}"
	local bdir="$TARGET_BDIR"
	if [ -d "$bdir" ]; then
		rm -rf "$bdir"
		log "zccli: removed $bdir"
	else
		log "zccli: nothing to clean for '$TARGET_BOARD'"
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

	# Demo apps that `zccli sample` can build.
	local -a sample_names=()
	local sn
	while IFS= read -r sn; do
		sample_names+=("$sn")
	done < <(list_samples)
	if [ "${#sample_names[@]}" -gt 0 ]; then
		log "  ok    samples (${#sample_names[@]}): ${sample_names[*]}"
		ok=$((ok + 1))
	else
		log "  warn  no sample apps bundled"
		warn=$((warn + 1))
	fi

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
	local sizefile line buildlabel
	resolve_last
	sizefile="$TARGET_BDIR/zccli-size.txt"
	buildlabel="$TARGET_BOARD"
	if [ "$TARGET_KIND" = "sample" ]; then
		buildlabel="$TARGET_NAME ($TARGET_BOARD)"
	fi
	if [ -f "$sizefile" ]; then
		log ""
		log "  last build -- $buildlabel:"
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
sample | samples) cmd_sample "$@" ;;
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
