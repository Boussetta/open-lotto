# SPDX-FileCopyrightText: 2026 Wissem Boussetta
# SPDX-License-Identifier: MIT

#!/usr/bin/env bash
set -euo pipefail

GAME="${1:-Lotto 6aus49}"
MAX_FETCH="${2:-200}"
WORKER_LIST="${3:-1,2,4,6,8}"
RUNS="${4:-2}"
BUILD_DIR="${BUILD_DIR:-${PWD}/build}"
BINARY="${BUILD_DIR}/open-lotto"
HISTORY_DIR="${HOME}/.local/share/open-lotto/history"
OUTPUT_CSV="${PWD}/build/download_worker_benchmark.csv"

if [[ ! -x "${BINARY}" ]]; then
  echo "Error: ${BINARY} not found. Build first." >&2
  exit 1
fi

if [[ ! -f "${HOME}/.config/open-lotto/sources.conf" ]]; then
  echo "Error: ${HOME}/.config/open-lotto/sources.conf is missing." >&2
  echo "Create it from config/sources.conf.example first." >&2
  exit 1
fi

mkdir -p "$(dirname "${OUTPUT_CSV}")"

slug=$(echo "${GAME}" | tr '[:upper:]' '[:lower:]' | sed -E 's/[^a-z0-9]+/_/g; s/^_+//; s/_+$//')
history_file="${HISTORY_DIR}/${slug}_gewinnzahlen.json"

echo "Benchmarking download workers"
echo "  game: ${GAME}"
echo "  max fetch draws: ${MAX_FETCH}"
echo "  workers: ${WORKER_LIST}"
echo "  runs per worker: ${RUNS}"
echo

printf "worker,run,seconds\n" > "${OUTPUT_CSV}"

best_worker=""
best_avg=""

IFS=',' read -r -a workers <<< "${WORKER_LIST}"

for w in "${workers[@]}"; do
  total=0
  count=0

  echo "== Worker ${w} =="

  for run in $(seq 1 "${RUNS}"); do
    rm -f "${history_file}"

    timing_file=$(mktemp)

    OPEN_LOTTO_HIST_DOWNLOAD_WORKERS="${w}" \
    OPEN_LOTTO_HIST_MAX_FETCH_DRAWS="${MAX_FETCH}" \
    /usr/bin/time -f "%e" -o "${timing_file}" \
      "${BINARY}" --game "${GAME}" --database-gewinnzahlen update >/dev/null 2>&1

    seconds=$(cat "${timing_file}")
    rm -f "${timing_file}"

    printf "%s,%s,%s\n" "${w}" "${run}" "${seconds}" >> "${OUTPUT_CSV}"

    total=$(awk -v a="${total}" -v b="${seconds}" 'BEGIN {printf "%.6f", a+b}')
    count=$((count + 1))

    echo "  run ${run}: ${seconds}s"
  done

  avg=$(awk -v t="${total}" -v c="${count}" 'BEGIN {printf "%.6f", t/c}')
  echo "  avg: ${avg}s"

  if [[ -z "${best_avg}" ]] || awk -v a="${avg}" -v b="${best_avg}" 'BEGIN {exit !(a < b)}'; then
    best_avg="${avg}"
    best_worker="${w}"
  fi

  echo

done

echo "Recommended worker count: ${best_worker} (avg ${best_avg}s)"
echo "Raw results: ${OUTPUT_CSV}"
