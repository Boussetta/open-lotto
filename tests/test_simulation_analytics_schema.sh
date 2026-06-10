#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Wissem Boussetta
# SPDX-License-Identifier: MIT
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
SCHEMA_FILE="$ROOT_DIR/docs/schemas/simulation_analytics_v1.schema.json"
HEADER_FILE="$ROOT_DIR/include/simulation_analytics_metadata.h"

if [[ ! -f "$SCHEMA_FILE" ]]; then
  echo "schema missing: $SCHEMA_FILE"
  exit 1
fi

if ! grep -q '"const": "simulation-analytics/v1"' "$SCHEMA_FILE"; then
  echo "schema version const mismatch"
  exit 1
fi

if ! grep -q 'SIM_ANALYTICS_SCHEMA_VERSION "simulation-analytics/v1"' "$HEADER_FILE"; then
  echo "metadata schema version constant mismatch"
  exit 1
fi

echo "simulation analytics schema contract OK"
