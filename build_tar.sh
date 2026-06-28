#!/bin/sh
# build_tar.sh - Create a tarball release for dynamic TDE build

set -e

PACKAGE_NAME="tuneperfs-gui"
VERSION=$(grep -oP 'TUNEPERF_VERSION="\K[0-9.]+' tuneperf.sh || echo "1.0")
ARCH="amd64"
if command -v dpkg >/dev/null 2>&1; then
    ARCH=$(dpkg --print-architecture)
fi
RELEASE_NAME="${PACKAGE_NAME}_${VERSION}_${ARCH}_dynamic"

echo "=== Preparing Tarball Build Directory ==="
rm -rf "$RELEASE_NAME"
rm -f "${RELEASE_NAME}.tar.gz"
mkdir -p "$RELEASE_NAME"

# Ensure binary is compiled
if [ "${1:-}" = "--no-rebuild" ] || [ "${1:-}" = "-n" ]; then
    echo "=== Skipping compilation as requested ==="
elif [ "${1:-}" = "--clean" ] || [ "${1:-}" = "-c" ]; then
    echo "=== Compiling TunePerf in Release mode (clean build) ==="
    rm -rf build
    ./build.sh
else
    echo "=== Compiling TunePerf in Release mode (incremental build) ==="
    ./build.sh
fi

SRC_BIN="build/src/tuneperfs-gui"
if [ ! -f "$SRC_BIN" ]; then
    echo "Error: '$SRC_BIN' binary not found. Compilation failed?" >&2
    exit 1
fi

# Copy and strip binary
cp "$SRC_BIN" "$RELEASE_NAME/tuneperfs-gui"
if command -v strip >/dev/null 2>&1; then
    strip --strip-all "$RELEASE_NAME/tuneperfs-gui" || true
fi

# Create simple run instructions
cat << EOF > "$RELEASE_NAME/README.txt"
TunePerf GUI - Trinity Desktop Environment (TDE) Build
Version: $VERSION
Architecture: $ARCH

This is the dynamically linked version of the TunePerf GUI.
It requires a functioning Trinity Desktop Environment (TDE) and TQt3 packages installed on the system.

Usage:
  ./tuneperfs-gui

Note:
  Do not run this binary with 'sudo' or as root. The GUI runs in your user session
  and will securely prompt for administrative credentials when performing backend changes.
EOF

echo "=== Creating Tarball ==="
tar -czf "${RELEASE_NAME}.tar.gz" "$RELEASE_NAME"
rm -rf "$RELEASE_NAME"

echo "=== Tarball Created successfully: ${RELEASE_NAME}.tar.gz ==="
