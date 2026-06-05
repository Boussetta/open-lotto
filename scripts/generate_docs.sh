#!/usr/bin/env bash
# scripts/generate_docs.sh
# Generate Doxygen HTML documentation for open-lotto.
# Output is written to docs/api/html/index.html.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

cd "$REPO_ROOT"

if ! command -v doxygen &>/dev/null; then
    echo "Error: doxygen is not installed. Install it with:" >&2
    echo "  sudo apt-get install doxygen" >&2
    exit 1
fi

echo "Generating API documentation..."
mkdir -p docs/api
doxygen Doxyfile

echo ""
echo "Documentation generated at: docs/api/html/index.html"
