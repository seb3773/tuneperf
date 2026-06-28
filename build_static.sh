#!/usr/bin/env bash
################################################
# build_static.sh — Build statique TQt3 (libtqt-mt.a embarquée, sans TDE)
#
# Produit : build-static/src/tuneperfs-gui (TQt3 statique, libs système dyn.)
#
# Usage :
#   ./build_static.sh
################################################
set -euo pipefail

SRC_ROOT="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="$SRC_ROOT/build-static"
TQT3_STATIC_DIR="$SRC_ROOT/libs/tqt3-static"

need_cmd() {
	if ! command -v "$1" >/dev/null 2>&1; then
		echo "Missing tool: $1" 1>&2
		return 1
	fi
	return 0
}

missing=0
need_cmd cmake || missing=1
need_cmd bash || missing=1

if test "$missing" -ne 0; then
	echo "" 1>&2
	echo "Some dependencies are missing." 1>&2
	echo "Install suggestions (examples):" 1>&2
	echo "- Debian/Ubuntu: sudo apt-get install cmake" 1>&2
	exit 1
fi

# Vérifie que la lib statique a été préparée
if [ ! -f "$TQT3_STATIC_DIR/lib/libtqt-mt.a" ]; then
	echo "La lib TQt3 statique n'est pas préparée." 1>&2
	echo "Lancez d'abord : bash setup-tqt3-static.sh" 1>&2
	exit 1
fi

mkdir -p -- "$BUILD_DIR"

cmake -S "$SRC_ROOT" -B "$BUILD_DIR" -DSTATIC_BUILD=ON
cmake --build "$BUILD_DIR" -j"$(nproc)"

BIN="$BUILD_DIR/src/tuneperfs-gui"
if [ ! -f "$BIN" ]; then
	echo "✗ Binaire introuvable : $BIN" 1>&2
	exit 1
fi

echo ""
echo "✓ Build statique terminé : $BIN"
ls -la "$BIN" | awk '{printf "  taille (strippé) : %.1f Ko\n", $5/1024}'
