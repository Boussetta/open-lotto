#!/usr/bin/env bash
# macOS code signing and notarization script
# Usage: ./sign-and-notarize.sh <binary> <identity> <team-id> <password>

set -euo pipefail

BINARY="$1"
IDENTITY="${2:-Developer ID Application: Wissem Boussetta (${3})}"
TEAM_ID="${3}"
APPLE_ID="${4}"
PASSWORD="${5}"

if [ ! -f "$BINARY" ]; then
    echo "Error: Binary not found: $BINARY"
    exit 1
fi

if [ -z "$TEAM_ID" ] || [ -z "$APPLE_ID" ] || [ -z "$PASSWORD" ]; then
    echo "Usage: $0 <binary> <identity> <team-id> <apple-id> <password>"
    echo ""
    echo "Environment variables can also be set:"
    echo "  DEVELOPER_ID_APPLICATION: identity for code signing"
    echo "  TEAM_ID: Apple Team ID"
    echo "  APPLE_ID: Apple ID for notarization"
    echo "  NOTARIZE_PASSWORD: app-specific password for notarization"
    exit 1
fi

echo "Signing $BINARY with identity: $IDENTITY"

# Step 1: Code sign the binary
codesign --force --verify --verbose --sign "$IDENTITY" "$BINARY"

# Step 2: Verify signature
codesign --verify --verbose "$BINARY"

echo "✓ Binary signed successfully"

# Step 3: Notarize (optional but recommended)
echo "Submitting for notarization..."

# Create a ZIP archive for notarization
ZIP_FILE="${BINARY%.app}.zip"
ditto -c -k --sequesterRsrc "$BINARY" "$ZIP_FILE"

# Submit for notarization
NOTARIZE_UUID=$(xcrun notarytool submit "$ZIP_FILE" \
    --apple-id "$APPLE_ID" \
    --password "$PASSWORD" \
    --team-id "$TEAM_ID" \
    --wait | grep -oP 'id: \K.*')

echo "✓ Notarization submitted, UUID: $NOTARIZE_UUID"

# Check status
xcrun notarytool log "$NOTARIZE_UUID" \
    --apple-id "$APPLE_ID" \
    --password "$PASSWORD" \
    --team-id "$TEAM_ID"

# Staple the ticket to the app
xcrun stapler staple "$BINARY"

echo "✓ Notarization ticket stapled to $BINARY"
echo "✓ Binary is ready for distribution"

# Cleanup
rm -f "$ZIP_FILE"
