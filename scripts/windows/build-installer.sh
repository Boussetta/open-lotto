# SPDX-FileCopyrightText: 2025 Wissem Boussetta
# SPDX-License-Identifier: MIT

#!/usr/bin/env bash
# Build script for Windows NSIS installer
# This script runs on Windows with NSIS installed
# Usage: ./build-windows-installer.sh <version> <binary-path>

set -euo pipefail

VERSION="${1:-0.7.0}"
BINARY_PATH="${2:-./build/open-lotto.exe}"

echo "Building Windows NSIS installer for open-lotto $VERSION"

# Check dependencies
if ! command -v makensis &> /dev/null; then
    echo "Error: NSIS is not installed."
    echo "Download from: https://nsis.sourceforge.io/Download"
    echo "Or install with: choco install nsis"
    exit 1
fi

if [ ! -f "$BINARY_PATH" ]; then
    echo "Error: Binary not found: $BINARY_PATH"
    exit 1
fi

# Check for DLL dependencies (assume they're in the build directory)
BUILD_DIR=$(dirname "$BINARY_PATH")
REQUIRED_DLLS=("SDL2.dll" "SDL2_ttf.dll")

for dll in "${REQUIRED_DLLS[@]}"; do
    if [ ! -f "$BUILD_DIR/$dll" ]; then
        echo "Warning: $dll not found in $BUILD_DIR"
        echo "The installer will include this file, but it must be present at build time."
    fi
done

# Create installer directory
INSTALLER_DIR="windows-installer-$VERSION"
mkdir -p "$INSTALLER_DIR"

# Copy binary and DLLs
cp "$BINARY_PATH" "$INSTALLER_DIR/"
if [ -f "$BUILD_DIR/SDL2.dll" ]; then
    cp "$BUILD_DIR/SDL2.dll" "$INSTALLER_DIR/"
fi
if [ -f "$BUILD_DIR/SDL2_ttf.dll" ]; then
    cp "$BUILD_DIR/SDL2_ttf.dll" "$INSTALLER_DIR/"
fi

# Update version in NSI script
NSI_SCRIPT="$INSTALLER_DIR/open-lotto-installer.nsi"
cp scripts/windows/open-lotto-installer.nsi "$NSI_SCRIPT"
sed -i "s/0.7.0/$VERSION/g" "$NSI_SCRIPT"

# Build the installer
OUTPUT="open-lotto-x64-setup.exe"
makensis "$NSI_SCRIPT"

if [ -f "$OUTPUT" ]; then
    echo "✓ Installer built successfully: $OUTPUT"
    sha256sum "$OUTPUT" > "${OUTPUT}.sha256"
    echo "✓ Checksum: $(cat ${OUTPUT}.sha256)"
else
    echo "Error: Installer build failed"
    exit 1
fi
