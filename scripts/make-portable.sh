#!/usr/bin/env bash
# ===========================================================================
#  make-portable.sh  --  Build a self-contained, portable Pixee for Linux.
#
#  The Linux counterpart to make-portable.bat. Produces
#      <repo>/Pixee-portable/                  (folder that runs with no Qt
#                                               installed)
#      <repo>/Pixee-<ver>-portable.tar.gz      archive, <ver> from VERSION.txt
#
#  Usage:
#      scripts/make-portable.sh [options]
#
#      --qmake=PATH     qmake to build with (default: $PIXEE_QMAKE, then
#                       qmake6, then qmake -- must be Qt 6)
#      --jobs=N         parallel make jobs (default: all cores)
#      --out=DIR        output folder (default: <repo>/Pixee-portable)
#      --bundle-stdcxx  also bundle libstdc++/libgcc_s. Off by default: they
#                       are excluded so the host's GL driver keeps using the
#                       system copy. Turn on only when targeting a distro
#                       older than the build machine.
#      --no-archive     leave the folder, skip the .tar.gz
#      --keep-work      keep the intermediate build dir for debugging
#      -h, --help       this text
#
#  Unlike Windows there is no windeployqt, so this script does the deploy
#  itself: it walks the binary's shared-library dependencies with ldd,
#  copies everything that is not part of a base Linux system into lib/,
#  copies the Qt plugins the app actually needs into plugins/, then repeats
#  the dependency walk over those plugins (the xcb platform plugin drags in
#  libQt6XcbQpa, which the main binary never references).
#
#  Exiv2: auto-detected from thirdparty/exiv2/lib/libexiv2.so* (the Linux
#  equivalent of the .bat's MSVC drop-in, see docs/metadata.md). Present ->
#  build with PIXEE_HAVE_EXIV2=1 and bundle the lib + its GPLv2+ licence;
#  absent -> the metadata panel shows Qt-only basics.
# ===========================================================================

set -euo pipefail

# ---- Args -----------------------------------------------------------------
QMAKE_BIN="${PIXEE_QMAKE:-}"
JOBS="$(nproc 2>/dev/null || echo 4)"
OUT_DIR_ARG=""
BUNDLE_STDCXX=0
MAKE_ARCHIVE=1
KEEP_WORK=0

for arg in "$@"; do
    case "$arg" in
        --qmake=*)      QMAKE_BIN="${arg#*=}" ;;
        --jobs=*)       JOBS="${arg#*=}" ;;
        --out=*)        OUT_DIR_ARG="${arg#*=}" ;;
        --bundle-stdcxx) BUNDLE_STDCXX=1 ;;
        --no-archive)   MAKE_ARCHIVE=0 ;;
        --keep-work)    KEEP_WORK=1 ;;
        -h|--help)      sed -n '2,40p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *)              echo "ERROR: unknown option '$arg' (try --help)" >&2; exit 1 ;;
    esac
done

die() { echo; echo "*** Build FAILED: $*" >&2; exit 1; }

# ---- Resolve paths --------------------------------------------------------
SCRIPT_DIR="$(cd "$(dirname "$(readlink -f "$0")")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

# Version: single source is the repo-root VERSION.txt, so the archive name
# matches the app and the installer. Fall back to 0.0.0 if somehow missing.
APPVER="0.0.0"
[ -r "$REPO_ROOT/VERSION.txt" ] && APPVER="$(tr -d ' \t\r\n' < "$REPO_ROOT/VERSION.txt")"
[ -n "$APPVER" ] || APPVER="0.0.0"

WORK_DIR="$REPO_ROOT/build-portable-work"
OUT_DIR="${OUT_DIR_ARG:-$REPO_ROOT/Pixee-portable}"
ARCHIVE="$REPO_ROOT/Pixee-$APPVER-portable.tar.gz"

# ---- Locate a Qt 6 qmake --------------------------------------------------
if [ -z "$QMAKE_BIN" ]; then
    for c in qmake6 qmake; do
        command -v "$c" >/dev/null 2>&1 && { QMAKE_BIN="$c"; break; }
    done
fi
[ -n "$QMAKE_BIN" ] || die "no qmake found. Install Qt 6 or pass --qmake=PATH."
command -v "$QMAKE_BIN" >/dev/null 2>&1 || [ -x "$QMAKE_BIN" ] \
    || die "qmake not executable: $QMAKE_BIN"

QT_VERSION="$("$QMAKE_BIN" -query QT_VERSION)"
case "$QT_VERSION" in
    6.*) ;;
    *) die "$QMAKE_BIN is Qt $QT_VERSION; Pixee needs Qt 6.6+ (6.x required)." ;;
esac
QT_PLUGINS="$("$QMAKE_BIN" -query QT_INSTALL_PLUGINS)"

# ---- Exiv2 drop-in? -------------------------------------------------------
# Mirrors the .bat's auto-detect. No toolchain gating needed here: on Linux
# there is one ABI, so unlike the MinGW/MSVC split there is no kit that has
# to skip it.
EXIV2_QMAKE=()
EXIV2_LIB=""
for cand in "$REPO_ROOT"/thirdparty/exiv2/lib/libexiv2.so*; do
    [ -e "$cand" ] && { EXIV2_LIB="$cand"; break; }
done

echo
echo "=== Pixee portable build (Linux) ==="
echo "  qmake   : $QMAKE_BIN (Qt $QT_VERSION)"
echo "  Jobs    : $JOBS"
echo "  Output  : $OUT_DIR"
if [ -n "$EXIV2_LIB" ]; then
    EXIV2_QMAKE=("PIXEE_HAVE_EXIV2=1")
    echo "  Exiv2 metadata backend: ENABLED"
else
    echo "  Exiv2 metadata backend: not present - metadata panel shows basics only."
fi
echo

# ---- Clean previous artifacts ---------------------------------------------
rm -rf "$WORK_DIR" "$OUT_DIR"
rm -f "$ARCHIVE"
mkdir -p "$WORK_DIR" "$OUT_DIR"

# ---- [1/5] Configure ------------------------------------------------------
# RPATH so ./Pixee works without the launcher. Two non-obvious bits:
#
#   QMAKE_RPATHDIR, not a hand-rolled QMAKE_LFLAGS -Wl,-rpath: qmake knows
#   this value has to survive its own expansion *and* make's, and emits the
#   correctly escaped \$$ORIGIN. Writing the flag by hand leaks a literal
#   '$$' into the link line and the RPATH silently points nowhere.
#
#   --disable-new-dtags, because the modern default emits DT_RUNPATH, and
#   RUNPATH is NOT inherited by transitive dependencies. Qt's own libs carry
#   no RUNPATH of their own, so libQt6Gui's search for libQt6DBus/ICU/
#   harfbuzz would ignore ours and silently pick the *host's* copies -- the
#   bundle looks fine on the build machine and dies anywhere else. The older
#   DT_RPATH is inherited, so one entry on the exe covers the whole graph.
# Verified after the build rather than trusted; the launcher ships either way.
echo "[1/5] qmake..."
( cd "$WORK_DIR" && "$QMAKE_BIN" "$REPO_ROOT/Pixee.pro" \
    "CONFIG+=release" "CONFIG-=debug_and_release" \
    "DESTDIR=$OUT_DIR" \
    'QMAKE_RPATHDIR=$ORIGIN/lib' \
    'QMAKE_LFLAGS+=-Wl,--disable-new-dtags' \
    "${EXIV2_QMAKE[@]}" >/dev/null ) || die "qmake failed"

# ---- [2/5] Build ----------------------------------------------------------
echo "[2/5] make -j$JOBS..."
( cd "$WORK_DIR" && make -j"$JOBS" >/dev/null ) || die "make failed"
[ -x "$OUT_DIR/Pixee" ] || die "build finished but $OUT_DIR/Pixee is missing."

# ---- [3/5] Bundle shared libraries ----------------------------------------
# Libraries that belong to a base Linux install, or that MUST come from the
# host to work at all. Bundling the GL/X11/wayland/glib stack is the classic
# way to make a portable build crash on someone else's driver, so those stay
# on the host side. Everything else the binary needs (Qt, ICU, image codecs)
# gets copied in.
EXCLUDE_RE='^(ld-linux.*|libc|libm|libdl|libpthread|librt|libresolv|libnsl|libutil|libanl|libcrypt|libGL|libGLX|libGLdispatch|libGLU|libEGL|libOpenGL|libGLESv2|libdrm|libgbm|libX11|libX11-xcb|libXext|libXau|libXdmcp|libXrender|libXi|libXfixes|libXcursor|libXrandr|libXinerama|libXss|libXtst|libXcomposite|libXdamage|libxcb.*|libwayland.*|libglib-2\.0|libgobject-2\.0|libgio-2\.0|libgmodule-2\.0|libgthread-2\.0|libdbus-1|libfontconfig|libfreetype|libexpat|libz|libbz2|liblzma|libzstd|libselinux|libudev|libsystemd|libcap|libpcre|libmount|libblkid|libffi|libcom_err|libkrb5.*|libk5crypto|libgssapi_krb5|libkeyutils|libp11-kit|libtasn1|libidn2|libunistring|libgcrypt|libgpg-error|libssl|libcrypto|libcups|libasound|libpulse.*|libnss3|libnssutil3|libnspr4|libplc4|libplds4|libsmime3|libatk.*|libcairo.*|libgdk.*|libgtk.*|libpango.*|libatspi|libxkbcommon.*|libSM|libICE|libuuid|libthai|libdatrie|libgraphite2|libbsd|libmd)\.so'

mkdir -p "$OUT_DIR/lib"

is_excluded() {
    local base="$1"
    printf '%s' "$base" | grep -Eq "$EXCLUDE_RE" && return 0
    if [ "$BUNDLE_STDCXX" -eq 0 ]; then
        case "$base" in libstdc++.so*|libgcc_s.so*) return 0 ;; esac
    fi
    return 1
}

# Recursive dependency walk. Queue starts with the binary; every newly
# bundled library and every deployed plugin is fed back in, because a
# plugin's own dependencies (libQt6XcbQpa, image codecs) are invisible to
# ldd of the main executable.
declare -A BUNDLED=()
walk_deps() {
    local target="$1"
    local line soname path base
    while IFS= read -r line; do
        soname="$(printf '%s' "$line" | awk '{print $1}')"
        path="$(printf '%s' "$line" | awk '{print $3}')"
        # "linux-vdso.so.1 =>  (0x...)" and "statically linked" have no path
        [ -n "$path" ] && [ -e "$path" ] || continue
        base="$(basename "$soname")"
        is_excluded "$base" && continue
        [ -n "${BUNDLED[$base]:-}" ] && continue
        BUNDLED[$base]=1
        cp -L "$path" "$OUT_DIR/lib/$base"
        walk_deps "$OUT_DIR/lib/$base"
    done < <(ldd "$target" 2>/dev/null | grep '=>' || true)
}

echo "[3/5] bundling libraries..."
walk_deps "$OUT_DIR/Pixee"

# ---- [4/5] Qt plugins -----------------------------------------------------
# Only what Pixee actually uses. imageformats is the whole point of the app;
# sqldrivers/libqsqlite is the thumbnail cache at ~/.pixee/thumbnails.s3db --
# without it every thumbnail is regenerated on every run. platformthemes is
# deliberately skipped: libqgtk3 drags in the whole GTK stack, which the
# exclude list (correctly) refuses to bundle.
echo "[4/5] Qt plugins..."
deploy_plugin_dir() {
    local sub="$1" pattern="${2:-*.so}" src="$QT_PLUGINS/$1" f
    [ -d "$src" ] || return 0
    mkdir -p "$OUT_DIR/plugins/$sub"
    local found=0
    for f in "$src"/$pattern; do
        [ -e "$f" ] || continue
        cp -L "$f" "$OUT_DIR/plugins/$sub/"
        walk_deps "$OUT_DIR/plugins/$sub/$(basename "$f")"
        found=1
    done
    [ "$found" -eq 1 ] || rmdir "$OUT_DIR/plugins/$sub" 2>/dev/null || true
}

# Platform plugins by name: xcb and the wayland family are the real display
# backends; minimal/offscreen make the portable usable headless (CI, ssh).
for p in libqxcb.so libqminimal.so libqoffscreen.so libqwayland-egl.so \
         libqwayland-generic.so libqwayland-xcomposite-egl.so; do
    src="$QT_PLUGINS/platforms/$p"
    [ -e "$src" ] || continue
    mkdir -p "$OUT_DIR/plugins/platforms"
    cp -L "$src" "$OUT_DIR/plugins/platforms/"
    walk_deps "$OUT_DIR/plugins/platforms/$p"
done
deploy_plugin_dir imageformats
deploy_plugin_dir sqldrivers 'libqsqlite.so'
deploy_plugin_dir iconengines
deploy_plugin_dir xcbglintegrations
deploy_plugin_dir wayland-shell-integration
deploy_plugin_dir wayland-decoration-client
deploy_plugin_dir wayland-graphics-integration-client
deploy_plugin_dir platforminputcontexts

# Qt libs pulled in only by plugins land in lib/ via walk_deps above.

# ---- Exiv2 runtime + licence ----------------------------------------------
if [ -n "$EXIV2_LIB" ]; then
    for so in "$REPO_ROOT"/thirdparty/exiv2/lib/libexiv2.so*; do
        [ -e "$so" ] && cp -L "$so" "$OUT_DIR/lib/$(basename "$so")"
    done
    # Exiv2 is GPLv2+; ship its licence next to the binary (docs/metadata.md).
    for lic in COPYING LICENSE; do
        [ -r "$REPO_ROOT/thirdparty/exiv2/$lic" ] \
            && cp "$REPO_ROOT/thirdparty/exiv2/$lic" "$OUT_DIR/Exiv2-COPYING.txt" \
            && break
    done
    echo "  Bundled Exiv2 runtime + licence."
fi

# ---- qt.conf + launcher ---------------------------------------------------
# qt.conf sits next to the binary and points Qt at the bundled plugins, so
# the app does not pick up the host's Qt plugins (or fail to find any).
cat > "$OUT_DIR/qt.conf" <<'EOF'
[Paths]
Prefix = .
Plugins = plugins
Libraries = lib
EOF

# Launcher: the belt to the RPATH's braces. Also the thing to double-click.
cat > "$OUT_DIR/Pixee.sh" <<'EOF'
#!/bin/sh
# Portable Pixee launcher: run from anywhere, uses only the bundled Qt.
here=$(dirname "$(readlink -f "$0")")
LD_LIBRARY_PATH="$here/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export LD_LIBRARY_PATH
QT_PLUGIN_PATH="$here/plugins"
export QT_PLUGIN_PATH
exec "$here/Pixee" "$@"
EOF
chmod +x "$OUT_DIR/Pixee.sh"

# themes/ is copied next to the binary by Pixee.pro's copy_themes target
# (DESTDIR = OUT_DIR), and Config::appPath() is applicationDirPath() -- i.e.
# the real binary's directory, not the launcher's. Verify rather than assume.
[ -d "$OUT_DIR/themes" ] || cp -r "$REPO_ROOT/themes" "$OUT_DIR/themes"

# ---- RPATH check ----------------------------------------------------------
RPATH_OK=0
if command -v readelf >/dev/null 2>&1; then
    readelf -d "$OUT_DIR/Pixee" 2>/dev/null \
        | grep -E '(RUNPATH|RPATH)' | grep -Fq '$ORIGIN/lib' && RPATH_OK=1
fi

# ---- [5/5] Archive --------------------------------------------------------
echo "[5/5] archive..."
if [ "$MAKE_ARCHIVE" -eq 1 ]; then
    tar -czf "$ARCHIVE" -C "$(dirname "$OUT_DIR")" "$(basename "$OUT_DIR")"
else
    echo "  Skipped (--no-archive)."
fi

[ "$KEEP_WORK" -eq 1 ] || rm -rf "$WORK_DIR"

# ---- Summary --------------------------------------------------------------
LIB_COUNT=$(find "$OUT_DIR/lib" -name '*.so*' 2>/dev/null | wc -l)
PLUGIN_COUNT=$(find "$OUT_DIR/plugins" -name '*.so' 2>/dev/null | wc -l)
echo
echo "=== Done ==="
echo "  Portable folder: $OUT_DIR"
[ "$MAKE_ARCHIVE" -eq 1 ] && echo "  Archive        : $ARCHIVE"
echo "  Bundled        : $LIB_COUNT libraries, $PLUGIN_COUNT plugins"
if [ "$RPATH_OK" -eq 1 ]; then
    echo "  Entry point    : ./Pixee (RPATH \$ORIGIN/lib) or ./Pixee.sh"
else
    echo "  Entry point    : ./Pixee.sh  (RPATH not set -- the launcher is required)"
fi
echo
echo "Tip: test it where Qt is NOT on the library path to confirm it is"
echo "     self-contained, e.g."
echo "       env -u LD_LIBRARY_PATH $OUT_DIR/Pixee.sh"
