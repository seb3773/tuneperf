#!/usr/bin/env bash
################################################
# build.sh — Build dynamique TDE/Trinity (par défaut)
#
# Produit : build/src/tuneperfs-gui (lien dynamique vers libtde* + libtqt-mt)
################################################
set -euo pipefail

SRC_ROOT="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="$SRC_ROOT/build"

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

# Les outils TDE (tmoc/tqmoc/tde-config) résident dans /opt/trinity/bin
TDE_BIN="/opt/trinity/bin"
if [ -d "$TDE_BIN" ]; then
	export PATH="$TDE_BIN:$PATH"
fi

mkdir -p -- "$BUILD_DIR"

cmake -S "$SRC_ROOT" -B "$BUILD_DIR"
cmake --build "$BUILD_DIR" -j"$(nproc)"

BIN="$BUILD_DIR/src/tuneperfs-gui"
if [ -f "$BIN" ]; then
	echo ""
	echo "✓ Build dynamique TDE terminé : $BIN"
	ls -la "$BIN" | awk '{printf "  taille : %.1f Ko\n", $5/1024}'
fi
