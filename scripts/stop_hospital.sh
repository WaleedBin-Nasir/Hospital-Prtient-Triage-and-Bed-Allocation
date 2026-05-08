#!/usr/bin/env bash
# Project : Hospital Patient Triage & Bed Allocator
# Script  : stop_hospital.sh
# Group   : Group XX
# Members :
#   - Muhammad Abdullah Khan (24F-0626)
#   - Muhammad Zunair Haider (24F-590)
#   - Waleed bin Nasir (24F-0516)
# Date    : 2026-05-08
# Purpose : Gracefully stop hospital system and clean IPC resources.
# Usage   : ./scripts/stop_hospital.sh
TRIAGE_FIFO="/tmp/hospital_triage.fifo"
SIM_FIFO="/tmp/discharge_fifo"
PID_FILE="/tmp/hospital_admissions.pid"
SHM_NAME="/hospital_beds"

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$SCRIPT_DIR/.."

echo "Hospital Triage System - Shutdown"
echo "---------------------------------"
echo ""

if [ "${1:-}" = "-h" ] || [ "${1:-}" = "--help" ]; then
    echo "Usage: ./scripts/stop_hospital.sh"
    echo "Stops admissions manager and removes IPC resources."
    exit 0
fi

# ── Send SIGTERM to Admissions Manager ────────────────────────
if [ -f "$PID_FILE" ]; then
    pid=$(cat "$PID_FILE")
    if kill -0 "$pid" 2>/dev/null; then
        echo "[1/5] Sending SIGTERM to Admissions Manager (PID=${pid}) ..."
        kill -TERM "$pid"
        # Wait up to 5s for graceful exit
        for i in $(seq 1 5); do
            sleep 1
            kill -0 "$pid" 2>/dev/null || break
        done
        if kill -0 "$pid" 2>/dev/null; then
            echo "WARNING: Process still alive, sending SIGKILL ..."
            kill -KILL "$pid" 2>/dev/null || true
        fi
        echo "Admissions Manager stopped."
    else
        echo "[1/5] Admissions Manager (PID=${pid}) is not running."
    fi
    rm -f "$PID_FILE"
else
    echo "[1/5] No PID file found - system may not have been running."
fi

# ── Kill any lingering patient_simulator children ─────────────
echo "[2/5] Cleaning up patient_simulator processes ..."
pkill -f patient_simulator 2>/dev/null && echo "Simulators stopped." || echo "None found."

# ── Remove FIFOs ──────────────────────────────────────────────
echo "[3/5] Removing FIFOs ..."
rm -f "$TRIAGE_FIFO" "$SIM_FIFO"
echo "FIFOs removed."

# ── Remove shared memory ──────────────────────────────────────
echo "[4/5] Removing shared memory ..."
rm -f "/dev/shm${SHM_NAME}" 2>/dev/null
echo "Shared memory removed."

# ── Print Final Ward Status Summary ──────────────────────────
echo "[5/5] Final Ward Status Summary ..."
echo ""
SCHEDULE_LOG="$ROOT/schedule_log.txt"
MEMORY_LOG="$ROOT/memory_log.txt"

patients=0
beds=0
if [ -f "$SCHEDULE_LOG" ]; then
    patients=$(grep -c "^Patient " "$SCHEDULE_LOG" 2>/dev/null || echo 0)
    avg_wait=$(grep "Average Waiting Time" "$SCHEDULE_LOG" 2>/dev/null | tail -1)
    avg_turn=$(grep "Average Turnaround Time" "$SCHEDULE_LOG" 2>/dev/null | tail -1)
fi
if [ -f "$ROOT/logs/admissions.log" ]; then
    beds=$(grep -c "AFTER COALESCE" "$ROOT/logs/admissions.log" 2>/dev/null || echo 0)
fi

echo "Final Ward Status"
echo "-----------------"
echo "Total Patients Served: $patients"
echo "Beds Freed (discharges): $beds"
if [ -n "$avg_wait" ]; then
    echo "$avg_wait"
fi
if [ -n "$avg_turn" ]; then
    echo "$avg_turn"
fi
echo ""
echo "Hospital system shut down cleanly."
echo "You can restart with: ./scripts/start_hospital.sh"
