#!/usr/bin/env bash
# Project : Hospital Patient Triage & Bed Allocator
# Script  : start_hospital.sh
# Group   : Group XX
# Members :
#   - Muhammad Abdullah Khan (24F-0626)
#   - Muhammad Zunair Haider (24F-590)
#   - Waleed bin Nasir (24F-0516)
# Date    : 2026-05-08
# Purpose : Compile and launch the hospital system.
# Usage   : ./scripts/start_hospital.sh [BEST_FIT|FIRST_FIT|WORST_FIT]
set -e
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$SCRIPT_DIR/.."
TRIAGE_FIFO="/tmp/hospital_triage.fifo"
SIM_FIFO="/tmp/discharge_fifo"
PID_FILE="/tmp/hospital_admissions.pid"
LOG_DIR="$ROOT/logs"

echo "Hospital Triage System - Startup"
echo "--------------------------------"

if [ "${1:-}" = "-h" ] || [ "${1:-}" = "--help" ]; then
    echo "Usage: ./scripts/start_hospital.sh [BEST_FIT|FIRST_FIT|WORST_FIT]"
    echo "Starts admissions manager, creates FIFOs, and writes logs to logs/."
    exit 0
fi

mkdir -p "$LOG_DIR"

if [ -f "$PID_FILE" ] && kill -0 "$(cat "$PID_FILE")" 2>/dev/null; then
    echo "WARNING: Admissions Manager already running (PID=$(cat "$PID_FILE"))."
    echo "Run stop_hospital.sh first if you want a fresh start."
    exit 1
fi

alloc="${1:-BEST_FIT}"
case "$alloc" in
    BEST_FIT|FIRST_FIT|WORST_FIT) ;;
    *)
        echo "ERROR: Invalid allocator '$alloc'."
        echo "Use: BEST_FIT, FIRST_FIT, or WORST_FIT"
        exit 1
        ;;
esac

echo "[1/4] Compiling with allocator strategy: $alloc ..."
cd "$ROOT"
make clean --quiet 2>/dev/null || true
make all ALLOC="$alloc" 2>&1 | tee logs/build.log
echo "Compile OK."

echo "[2/4] Creating FIFOs ..."
[ -p "$TRIAGE_FIFO" ] || mkfifo "$TRIAGE_FIFO"
[ -p "$SIM_FIFO"    ] || mkfifo "$SIM_FIFO"
echo "FIFOs ready."

echo "[3/4] Starting Admissions Manager ..."
cd "$ROOT"
./admissions_manager >> logs/admissions.log 2>&1 &
am_pid=$!
sleep 1

if ! kill -0 "$am_pid" 2>/dev/null; then
    echo "ERROR: Admissions Manager failed to start. Check logs/admissions.log"
    exit 1
fi

echo "Admissions Manager running (PID=$am_pid)."

echo "[4/4] System ready!"
echo ""
echo "Admit patients : ./scripts/triage.sh"
echo "Stress test    : ./scripts/stress_test.sh"
echo "Follow logs    : tail -f logs/admissions.log"
echo "Stop system    : ./scripts/stop_hospital.sh"
echo "Quick status   : ps -p $am_pid -o pid,cmd"
