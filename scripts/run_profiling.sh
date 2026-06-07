#!/usr/bin/env bash
# run_profiling.sh – Deep memory and CPU profiling for Open-Lotto benchmark
#
# Usage:
#   ./scripts/run_profiling.sh [--build-dir <dir>] [--output-dir <dir>]
#                              [--iterations <n>] [--valgrind] [--perf]
#                              [--all]
#
# What it does
# ------------
#   --valgrind   Run Valgrind --tool=massif to profile heap allocations and
#                generate a human-readable massif report (requires valgrind).
#   --perf       Run perf stat to capture CPU cycles, instructions, and L1/L2/L3
#                cache miss rates (requires Linux perf).
#   --all        Enable both --valgrind and --perf.
#
# If neither --valgrind nor --perf is given the script only runs the built-in
# benchmark profiling (memory snapshot, RDTSC timing, regression check).
#
# Exit codes
#   0  – all profiling passed / no regressions
#   1  – memory regression detected or a required tool is missing
set -euo pipefail

# ---------------------------------------------------------------------------
# Defaults
# ---------------------------------------------------------------------------
BUILD_DIR="${PWD}/build"
OUTPUT_DIR="${PWD}/build/profiling"
ITERATIONS=100000
RUN_VALGRIND=0
RUN_PERF=0

# ---------------------------------------------------------------------------
# Argument parsing
# ---------------------------------------------------------------------------
while [[ $# -gt 0 ]]; do
  case "$1" in
    --build-dir)   BUILD_DIR="$2";   shift 2 ;;
    --output-dir)  OUTPUT_DIR="$2";  shift 2 ;;
    --iterations)  ITERATIONS="$2";  shift 2 ;;
    --valgrind)    RUN_VALGRIND=1;   shift   ;;
    --perf)        RUN_PERF=1;       shift   ;;
    --all)         RUN_VALGRIND=1; RUN_PERF=1; shift ;;
    -h|--help)
      sed -n '2,30p' "$0"   # print the usage comment
      exit 0
      ;;
    *) echo "Unknown option: $1" >&2; exit 1 ;;
  esac
done

BENCHMARK="${BUILD_DIR}/benchmark"
if [[ ! -x "${BENCHMARK}" ]]; then
  echo "Error: benchmark binary not found at ${BENCHMARK}" >&2
  echo "Build first with: cmake --build ${BUILD_DIR} --target benchmark" >&2
  exit 1
fi

mkdir -p "${OUTPUT_DIR}"

# ---------------------------------------------------------------------------
# 1. Built-in profiling (always runs)
# ---------------------------------------------------------------------------
echo "============================================================"
echo "Built-in benchmark profiling  (iterations=${ITERATIONS})"
echo "============================================================"
"${BENCHMARK}" "${ITERATIONS}" || BENCH_RC=$?
BENCH_RC="${BENCH_RC:-0}"

if [[ "${BENCH_RC}" -ne 0 ]]; then
  echo ""
  echo "⚠  Built-in benchmark exited with code ${BENCH_RC} (possible memory regression)."
fi

# ---------------------------------------------------------------------------
# 2. Valgrind / Massif heap profiling
# ---------------------------------------------------------------------------
if [[ "${RUN_VALGRIND}" -eq 1 ]]; then
  echo ""
  echo "============================================================"
  echo "Valgrind Massif heap profiling"
  echo "============================================================"

  if ! command -v valgrind &>/dev/null; then
    echo "Error: valgrind not found in PATH.  Install with:" >&2
    echo "  sudo apt-get install valgrind  (Debian/Ubuntu)" >&2
    echo "  sudo dnf install valgrind      (Fedora/RHEL)" >&2
    exit 1
  fi

  MASSIF_OUT="${OUTPUT_DIR}/massif.out"
  MASSIF_REPORT="${OUTPUT_DIR}/massif_report.txt"

  echo "Running: valgrind --tool=massif --pages-as-heap=yes ..."
  # Use a smaller iteration count for Massif – it is ~20x slower.
  MASSIF_ITERS=$(( ITERATIONS / 20 ))
  [[ "${MASSIF_ITERS}" -lt 100 ]] && MASSIF_ITERS=100

  valgrind \
    --tool=massif \
    --pages-as-heap=yes \
    --massif-out-file="${MASSIF_OUT}" \
    "${BENCHMARK}" "${MASSIF_ITERS}" 2>&1 | tail -5

  echo "Massif raw output : ${MASSIF_OUT}"

  if command -v ms_print &>/dev/null; then
    ms_print "${MASSIF_OUT}" > "${MASSIF_REPORT}"
    echo "Massif report     : ${MASSIF_REPORT}"
    # Show peak allocation line
    grep -m1 "^[ ]*[0-9]" "${MASSIF_REPORT}" | head -1 || true
  else
    echo "(ms_print not found – install valgrind tools to generate a text report)"
  fi
fi

# ---------------------------------------------------------------------------
# 3. perf stat – CPU cycles, instructions, cache misses
# ---------------------------------------------------------------------------
if [[ "${RUN_PERF}" -eq 1 ]]; then
  echo ""
  echo "============================================================"
  echo "perf stat – CPU and cache profiling"
  echo "============================================================"

  if ! command -v perf &>/dev/null; then
    echo "Error: perf not found in PATH.  Install with:" >&2
    echo "  sudo apt-get install linux-perf  (Debian/Ubuntu)" >&2
    echo "  sudo dnf install perf            (Fedora/RHEL)" >&2
    exit 1
  fi

  PERF_OUT="${OUTPUT_DIR}/perf_stat.txt"
  PERF_ITERS=$(( ITERATIONS / 10 ))
  [[ "${PERF_ITERS}" -lt 1000 ]] && PERF_ITERS=1000

  echo "Running: perf stat -e cycles,instructions,cache-misses,L1-dcache-load-misses,LLC-load-misses ..."
  perf stat \
    -e cycles,instructions,cache-misses,L1-dcache-load-misses,LLC-load-misses \
    -o "${PERF_OUT}" \
    -- "${BENCHMARK}" "${PERF_ITERS}" > /dev/null 2>&1 || true

  echo "perf stat output  : ${PERF_OUT}"
  cat "${PERF_OUT}"
fi

# ---------------------------------------------------------------------------
# Summary
# ---------------------------------------------------------------------------
echo ""
echo "============================================================"
echo "Profiling complete.  Artifacts in: ${OUTPUT_DIR}"
echo "============================================================"
ls -lh "${OUTPUT_DIR}"/ 2>/dev/null || true

exit "${BENCH_RC}"
