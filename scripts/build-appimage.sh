#!/usr/bin/env bash
# Build script for open-lotto AppImage
# Usage: ./build-appimage.sh [version]

set -euo pipefail

VERSION="${1:-0.7.0}"
ARCH="x86_64"
APP_NAME="open-lotto"

echo "Building $APP_NAME $VERSION AppImage for $ARCH"

# Check dependencies
for cmd in appimage-builder appimagetool linuxdeploy; do
    if ! command -v "$cmd" &> /dev/null; then
        echo "Error: $cmd is not installed. Install with:"
        echo "  pip install appimage-builder"
        echo "  or use: apt install appimagetool"
        exit 1
    fi
done

# Build the application
mkdir -p build-appimage
cd build-appimage
cmake -DCMAKE_BUILD_TYPE=Release ..
cmake --build . --config Release
cd ..

# Create AppDir structure
APPDIR="AppDir-$VERSION"
rm -rf "$APPDIR"
mkdir -p "$APPDIR/usr/bin"
mkdir -p "$APPDIR/usr/share/applications"
mkdir -p "$APPDIR/usr/share/pixmaps"

# Copy binary
cp build-appimage/open-lotto "$APPDIR/usr/bin/"

# Copy desktop entry
cat > "$APPDIR/usr/share/applications/open-lotto.desktop" << 'EOF'
[Desktop Entry]
Type=Application
Name=Open Lotto
Comment=Modular lottery number generator
Exec=open-lotto
Icon=open-lotto
Categories=Utility;
EOF

# Use linuxdeploy to bundle dependencies
linuxdeploy-x86_64.AppImage \
    --appdir="$APPDIR" \
    --output=appimage \
    --custom-apprun="$APPDIR/AppRun"

# Rename output
OUTPUT="$APP_NAME-$ARCH.AppImage"
if [ -f "Open_Lotto-$VERSION-x86_64.AppImage" ]; then
    mv "Open_Lotto-$VERSION-x86_64.AppImage" "$OUTPUT"
elif [ -f "$(ls -1 *.AppImage 2>/dev/null | head -1)" ]; then
    mv "$(ls -1 *.AppImage | head -1)" "$OUTPUT"
fi

# Make executable
chmod +x "$OUTPUT"

# Generate checksum
sha256sum "$OUTPUT" > "${OUTPUT}.sha256"

echo "✓ AppImage built successfully: $OUTPUT"
echo "✓ Checksum: $(cat ${OUTPUT}.sha256)"
