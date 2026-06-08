#!/usr/bin/env bash
# Build script for open-lotto Flatpak
# Usage: ./build-flatpak.sh

set -euo pipefail

APP_ID="com.boussetta.openlotto"
MANIFEST="scripts/flatpak/com.boussetta.openlotto.yml"

echo "Building Flatpak for $APP_ID"

# Check dependencies
if ! command -v flatpak &> /dev/null; then
    echo "Error: flatpak is not installed."
    echo "Install with: sudo apt install flatpak flatpak-builder"
    exit 1
fi

if ! command -v flatpak-builder &> /dev/null; then
    echo "Error: flatpak-builder is not installed."
    echo "Install with: sudo apt install flatpak-builder"
    exit 1
fi

# Build the Flatpak
BUILD_DIR="flatpak-build"
REPO_DIR="flatpak-repo"

mkdir -p "$BUILD_DIR" "$REPO_DIR"

echo "Building Flatpak from manifest: $MANIFEST"
flatpak-builder \
    --repo="$REPO_DIR" \
    --disable-rofiles-fuse \
    --install-deps-from=flathub \
    --force-clean \
    "$BUILD_DIR" \
    "$MANIFEST"

echo "✓ Flatpak built successfully"
echo ""
echo "Next steps:"
echo "1. Test the Flatpak:"
echo "   flatpak install --user file://$REPO_DIR $APP_ID"
echo "   flatpak run $APP_ID"
echo ""
echo "2. Publish to Flathub:"
echo "   Fork https://github.com/flathub/flathub"
echo "   Submit PR with manifest at flathub/$APP_ID.yml"
echo "   Follow: https://docs.flathub.org/docs/for-app-authors/submission"
