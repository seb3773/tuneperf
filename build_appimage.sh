#!/bin/env bash
# build_appimage.sh - Build AppImage for TunePerf GUI (Trinity/TDE Dynamic Build)
set -euo pipefail

SRC_ROOT="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="$SRC_ROOT/build"
APPDIR="$BUILD_DIR/AppDir"

need_cmd() {
	command -v "$1" >/dev/null 2>&1 || {
		echo "error: missing required command: $1" >&2
		exit 1
	}
}

need_cmd cmake
need_cmd pkg-config
need_cmd strip
need_cmd sed
need_cmd awk
need_cmd wget
need_cmd cp
need_cmd chmod
need_cmd mkdir

# Make sure build dir exists
mkdir -p -- "$BUILD_DIR"

# Build the binary using the existing build.sh script
echo "info: building tuneperfs-gui..."
"$SRC_ROOT/build.sh"

BIN_PATH="$BUILD_DIR/src/tuneperfs-gui"
if test ! -x "$BIN_PATH"; then
	echo "error: missing built binary: $BIN_PATH" >&2
	exit 1
fi

# Clean and create AppDir structure
echo "info: preparing AppDir..."
rm -rf -- "$APPDIR"
mkdir -p -- \
	"$APPDIR/usr/bin" \
	"$APPDIR/usr/lib" \
	"$APPDIR/usr/share/applications" \
	"$APPDIR/usr/share/icons/hicolor/64x64/apps"

# Copy binary
cp -a "$BIN_PATH" "$APPDIR/usr/bin/tuneperfs-gui"

# Strip staged binary
if command -v sstrip >/dev/null 2>&1; then
	echo "info: stripping staged binary with sstrip"
	sstrip "$APPDIR/usr/bin/tuneperfs-gui" >/dev/null 2>&1 || true
else
	echo "info: using strip --strip-all"
	strip --strip-all "$APPDIR/usr/bin/tuneperfs-gui" >/dev/null 2>&1 || true
fi

# Resolve and copy TQt3, TDE and other custom libraries from ldd output
echo "info: copying library dependencies..."
libraries=(
	libtqt-mt.so.3
	libtdecore.so.14
	libDCOP.so.14
	libtdefx.so.14
	libtqt.so.4
	libart_lgpl_2.so.2
)

# Run ldd and extract library paths
for lib in "${libraries[@]}"; do
	libpath=$(ldd "$BIN_PATH" | grep "$lib" | awk '{print $3}')
	if [[ -n "$libpath" && -f "$libpath" ]]; then
		echo "  -> bundling: $lib ($libpath)"
		cp -L "$libpath" "$APPDIR/usr/lib/"
	else
		echo "  warning: library $lib not resolved via ldd"
	fi
done

# Copy icon
ICON_SRC="$SRC_ROOT/icons/tuneperfs.png"
if test -f "$ICON_SRC"; then
	cp -a "$ICON_SRC" "$APPDIR/tuneperfs.png"
	cp -a "$ICON_SRC" "$APPDIR/usr/share/icons/hicolor/64x64/apps/tuneperfs.png"
else
	echo "error: missing $ICON_SRC" >&2
	exit 1
fi

# Create Desktop entry at root of AppDir and usr/share/applications
cat > "$APPDIR/tuneperfs-gui.desktop" <<EOF
[Desktop Entry]
Version=1.0
Name=TunePerf
GenericName=System Optimizer
Comment=Intelligent Linux system optimizer and manager GUI
Exec=tuneperfs-gui
Icon=tuneperfs
Terminal=false
Type=Application
Categories=System;Settings;
EOF
chmod 0644 "$APPDIR/tuneperfs-gui.desktop"
cp -a "$APPDIR/tuneperfs-gui.desktop" "$APPDIR/usr/share/applications/tuneperfs-gui.desktop"

# Create AppRun entry script
cat > "$APPDIR/AppRun" <<'EOF'
#!/bin/sh
SELF=$(readlink -f "$0")
HERE=${SELF%/*}
export LD_LIBRARY_PATH="$HERE/usr/lib:$LD_LIBRARY_PATH"
exec "$HERE/usr/bin/tuneperfs-gui" "$@"
EOF
chmod 0755 "$APPDIR/AppRun"

# Download appimagetool if not present
APPIMAGETOOL="$BUILD_DIR/appimagetool"
if [ ! -s "$APPIMAGETOOL" ]; then
	echo "info: downloading appimagetool..."
	wget -q --show-progress -O "$APPIMAGETOOL" "https://github.com/AppImage/appimagetool/releases/download/continuous/appimagetool-x86_64.AppImage"
	chmod +x "$APPIMAGETOOL"
fi

# Build AppImage using --appimage-extract-and-run to bypass FUSE requirements
OUT_APPIMAGE="$SRC_ROOT/tuneperfs-gui-x86_64.AppImage"
rm -f -- "$OUT_APPIMAGE"

echo "info: generating AppImage..."
# Set ARCH environment variable so appimagetool knows what architecture we are packaging
export ARCH=x86_64
"$APPIMAGETOOL" --appimage-extract-and-run "$APPDIR" "$OUT_APPIMAGE"

echo "AppImage successfully built: $OUT_APPIMAGE"
exit 0
