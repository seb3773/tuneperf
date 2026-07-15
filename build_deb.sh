#!/bin/sh
# build_deb.sh - Build Debian package for TunePerf GUI (Trinity/TDE)

set -e

PACKAGE_NAME="tuneperfs-gui"
VERSION=$(grep -oP 'TUNEPERF_VERSION="\K[0-9.]+' tuneperf.sh || echo "1.0")
ARCH="amd64"
if command -v dpkg >/dev/null 2>&1; then
    ARCH=$(dpkg --print-architecture)
fi
BUILD_DIR="${PACKAGE_NAME}_${VERSION}_${ARCH}"

echo "=== Preparing Debian Package Build Directory ==="
# Clean old build directory
rm -rf "$BUILD_DIR"
rm -f "${BUILD_DIR}.deb"

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

echo "Using binary: $SRC_BIN"

# Create package directory tree
mkdir -p "$BUILD_DIR/DEBIAN"
mkdir -p "$BUILD_DIR/usr/bin"
mkdir -p "$BUILD_DIR/usr/share/applications"
mkdir -p "$BUILD_DIR/usr/share/icons/hicolor"

# Copy binary
cp "$SRC_BIN" "$BUILD_DIR/usr/bin/tuneperfs-gui"
chmod 755 "$BUILD_DIR/usr/bin/tuneperfs-gui"

# Strip binary
if command -v strip >/dev/null 2>&1; then
    echo "Stripping binary..."
    strip --strip-all "$BUILD_DIR/usr/bin/tuneperfs-gui" || true
fi

# Generate desktop entry
cat << EOF > "$BUILD_DIR/usr/share/applications/tuneperfs-gui.desktop"
[Desktop Entry]
Name=TunePerf
Comment=Intelligent Linux System Optimizer
Exec=tuneperfs-gui
Icon=tuneperfs
Terminal=false
Type=Application
Categories=System;Settings;
EOF
chmod 644 "$BUILD_DIR/usr/share/applications/tuneperfs-gui.desktop"

# Package application icons
ICON_SRC="icons/tuneperfs.png"
if [ -f "$ICON_SRC" ]; then
    echo "Packaging icons..."
    REAL_SZ="32x32"
    REAL_DIR="$BUILD_DIR/usr/share/icons/hicolor/$REAL_SZ/apps"
    mkdir -p "$REAL_DIR"
    cp "$ICON_SRC" "$REAL_DIR/tuneperfs.png"
    chmod 644 "$REAL_DIR/tuneperfs.png"

    # Create symlinks for other standard sizes
    for sz in 16x16 22x22 24x24 48x48 64x64 128x128; do
        DST_DIR="$BUILD_DIR/usr/share/icons/hicolor/$sz/apps"
        mkdir -p "$DST_DIR"
        ln -sf "../../$REAL_SZ/apps/tuneperfs.png" "$DST_DIR/tuneperfs.png"
    done
fi

# Generate control file
cat << EOF > "$BUILD_DIR/DEBIAN/control"
Package: $PACKAGE_NAME
Version: $VERSION
Section: utils
Priority: optional
Architecture: $ARCH
Maintainer: seb3773
Depends: libtqt-mt | libtqt3-mt | libtqt-mt-trinity | libtqt3-mt-trinity, tdelibs14-trinity | tdelibs-trinity
Description: Intelligent Linux system optimizer and manager GUI
 TunePerf scans system topology to dynamically generate and apply
 optimized system kernel parameters, scheduler settings, network cache
 buffers, and SysV limits. It executes as a one-shot payload at boot.
 This package contains the TDE integration client.
EOF

# Generate postinst script
cat << 'EOF' > "$BUILD_DIR/DEBIAN/postinst"
#!/bin/sh
set -e

if command -v gtk-update-icon-cache >/dev/null 2>&1; then
    gtk-update-icon-cache -f -t /usr/share/icons/hicolor >/dev/null 2>&1 || true
fi
if command -v update-desktop-database >/dev/null 2>&1; then
    update-desktop-database -q /usr/share/applications >/dev/null 2>&1 || true
fi
EOF

# Generate prerm script
cat << 'EOF' > "$BUILD_DIR/DEBIAN/prerm"
#!/bin/sh
set -e

if command -v gtk-update-icon-cache >/dev/null 2>&1; then
    gtk-update-icon-cache -f -t /usr/share/icons/hicolor >/dev/null 2>&1 || true
fi
if command -v update-desktop-database >/dev/null 2>&1; then
    update-desktop-database -q /usr/share/applications >/dev/null 2>&1 || true
fi
EOF

# Set permissions of DEBIAN scripts
chmod 755 "$BUILD_DIR/DEBIAN/postinst"
chmod 755 "$BUILD_DIR/DEBIAN/prerm"

echo "=== Building Debian Package ==="
if command -v dpkg-deb >/dev/null 2>&1; then
    if dpkg-deb --help | grep -q -- "--root-owner-group"; then
        dpkg-deb --root-owner-group --build "$BUILD_DIR"
    elif command -v fakeroot >/dev/null 2>&1; then
        fakeroot dpkg-deb --build "$BUILD_DIR"
    else
        dpkg-deb --build "$BUILD_DIR"
    fi
    echo "=== Package Built successfully: ${BUILD_DIR}.deb ==="
    # Cleanup directory
    rm -rf "$BUILD_DIR"
else
    echo "Error: dpkg-deb command not found. Cannot build .deb package." >&2
    echo "Ensure you are on a Debian-based system to run this script." >&2
    exit 1
fi
