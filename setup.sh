#!/usr/bin/env bash
# Secondary install path: build from source into ~/.local.
# Preferred distribution is the AppImage from ./pack-appimage.sh
# (no system Qt/LayerShellQt install required to *run*).
#
# Dependencies: if already present, this is fully offline. Otherwise pacman
# fetches the missing packages (needs sudo + network).
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PREFIX="${PREFIX:-$HOME/.local}"
BUILD_DIR="${BUILD_DIR:-$ROOT/build}"
DESKTOP_DIR="${XDG_DESKTOP_DIR:-$HOME/Desktop}"

DEPS=(qt6-base qt6-wayland layer-shell-qt cmake gcc)

need_install=()
if command -v pacman >/dev/null 2>&1; then
    for pkg in "${DEPS[@]}"; do
        if ! pacman -Q "$pkg" >/dev/null 2>&1; then
            need_install+=("$pkg")
        fi
    done
    if ((${#need_install[@]} > 0)); then
        echo "Eksik paketler (online): ${need_install[*]}"
        sudo pacman -S --needed --noconfirm "${need_install[@]}"
    else
        echo "Bağımlılıklar zaten kurulu (offline derleme)."
    fi
else
    echo "Uyarı: pacman yok; qt6-base, qt6-wayland, layer-shell-qt, cmake, ninja, gcc gerekli." >&2
fi

cmake -S "$ROOT" -B "$BUILD_DIR" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX="$PREFIX"
cmake --build "$BUILD_DIR"
cmake --install "$BUILD_DIR"

BIN="$PREFIX/bin/system-overlay"
APPDIR="$PREFIX/share/applications"
DESKTOP_SRC="$APPDIR/system-overlay.desktop"

if [[ ! -x "$BIN" ]]; then
    echo "Kurulum başarısız: $BIN bulunamadı" >&2
    exit 1
fi

if command -v update-desktop-database >/dev/null 2>&1; then
    update-desktop-database "$APPDIR" 2>/dev/null || true
fi

if [[ -d "$DESKTOP_DIR" && -f "$DESKTOP_SRC" ]]; then
    cp "$DESKTOP_SRC" "$DESKTOP_DIR/system-overlay.desktop"
    chmod +x "$DESKTOP_DIR/system-overlay.desktop"
    echo "Masaüstü kısayolu: $DESKTOP_DIR/system-overlay.desktop"
fi

echo
echo "Kuruldu: $BIN"
echo "Uygulama menüsü: System Overlay"
echo "Çalıştırmak: $BIN"
echo "Tepsi ikonundan kapatılır (sağ tık → Çıkış)."
