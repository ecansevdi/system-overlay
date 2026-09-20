#!/usr/bin/env bash
# Build a self-contained x86_64 AppImage (Qt6 + LayerShellQt + Wayland plugins).
# Does not require FUSE at runtime for the *output*; the linuxdeploy tools
# themselves are run with APPIMAGE_EXTRACT_AND_RUN=1.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${BUILD_DIR:-$ROOT/build}"
DIST_DIR="${DIST_DIR:-$ROOT/dist}"
APPDIR="${APPDIR:-$ROOT/build/AppDir}"
TOOLS_DIR="${TOOLS_DIR:-$ROOT/.tools}"
ARCH="${ARCH:-x86_64}"
if [[ -z "${VERSION:-}" ]]; then
    VERSION="$(awk '/^project\(system-overlay/{f=1} f&&/VERSION/{
        if (match($0, /[0-9]+\.[0-9]+\.[0-9]+/)) { print substr($0, RSTART, RLENGTH); exit }
    }' "$ROOT/CMakeLists.txt")"
fi
VERSION="${VERSION:-1.4.1}"
OUT_NAME="system-overlay-${VERSION}-${ARCH}.AppImage"

LD_URL="https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-${ARCH}.AppImage"
QT_URL="https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases/download/continuous/linuxdeploy-plugin-qt-${ARCH}.AppImage"

mkdir -p "$TOOLS_DIR" "$DIST_DIR"

need() { command -v "$1" >/dev/null 2>&1 || { echo "Gerekli komut yok: $1" >&2; exit 1; }; }
need cmake
need qmake6
need magick

echo "==> Derleme"
cmake -S "$ROOT" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release
cmake --build "$BUILD_DIR"

BIN="$BUILD_DIR/system-overlay"
[[ -x "$BIN" ]] || { echo "Derleme çıktısı yok: $BIN" >&2; exit 1; }

echo "==> İkon"
ICON="$DIST_DIR/system-overlay.png"
# Tray icon scaled to 256px: dark rounded square, three green bars.
magick -size 256x256 xc:none \
  -fill 'rgba(24,24,24,0.90)' -draw 'roundrectangle 11,11 245,245 53,53' \
  -fill '#a6f28f' \
  -draw 'rectangle 59,144 96,212' \
  -draw 'rectangle 112,91 149,212' \
  -draw 'rectangle 165,59 202,212' \
  "$ICON"

echo "==> linuxdeploy araçları"
fetch() {
    local url="$1" dest="$2"
    if [[ -s "$dest" ]]; then
        return 0
    fi
    echo "    indiriliyor $(basename "$dest")"
    curl -L --fail --retry 3 -o "$dest" "$url"
    chmod +x "$dest"
}
fetch "$LD_URL" "$TOOLS_DIR/linuxdeploy-${ARCH}.AppImage"
fetch "$QT_URL" "$TOOLS_DIR/linuxdeploy-plugin-qt-${ARCH}.AppImage"

echo "==> AppDir"
rm -rf "$APPDIR"
mkdir -p "$APPDIR/usr/bin" "$APPDIR/usr/share/applications" \
         "$APPDIR/usr/share/icons/hicolor/256x256/apps"
install -m 755 "$BIN" "$APPDIR/usr/bin/system-overlay"
install -m 644 "$ROOT/resources/appimage/system-overlay.desktop" \
    "$APPDIR/usr/share/applications/system-overlay.desktop"
install -m 644 "$ICON" "$APPDIR/usr/share/icons/hicolor/256x256/apps/system-overlay.png"

echo "==> Bağımlılıkları paketle"
export APPIMAGE_EXTRACT_AND_RUN=1
export LINUXDEPLOY_OUTPUT_VERSION="$VERSION"
export OUTPUT="$DIST_DIR/$OUT_NAME"
export QMAKE="$(command -v qmake6)"
# Arch/CachyOS ELF (RELR) is newer than linuxdeploy's bundled strip.
export NO_STRIP=1
# Layer-shell needs the Wayland shell integration plugin (liblayer-shell.so).
export EXTRA_QT_PLUGINS="wayland;wayland-decoration-client;wayland-graphics-integration-client;wayland-shell-integration;iconengines;platforminputcontexts;platformthemes;styles;xcbglintegrations"
export EXTRA_PLATFORM_PLUGINS="libqwayland.so;libqxcb.so"

rm -f "$OUTPUT"
# Plugin AppImage must sit next to linuxdeploy so it is discovered.
ln -sfn "$TOOLS_DIR/linuxdeploy-plugin-qt-${ARCH}.AppImage" \
    "$TOOLS_DIR/linuxdeploy-plugin-qt"

"$TOOLS_DIR/linuxdeploy-${ARCH}.AppImage" \
    --appimage-extract-and-run \
    --appdir "$APPDIR" \
    --executable "$APPDIR/usr/bin/system-overlay" \
    --desktop-file "$APPDIR/usr/share/applications/system-overlay.desktop" \
    --icon-file "$ICON" \
    --plugin qt \
    --output appimage

chmod +x "$OUTPUT"

echo
echo "AppImage: $OUTPUT"
echo "Çalıştırmak: $OUTPUT"
echo "Sisteme bir şey kurulmaz; Qt/LayerShellQt paket içinde."
echo
echo "İkinci seçenek (kaynaktan kurulum): ./setup.sh"
