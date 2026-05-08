#!/usr/bin/env bash
# Project : Hospital Patient Triage & Bed Allocator
# Script  : stress_test.sh
# Group   : Group XX
# Members :
#   - Muhammad Abdullah Khan (24F-0626)
#   - Muhammad Zunair Haider (24F-590)
#   - Waleed bin Nasir (24F-0516)
# Date    : 2026-05-08
# Purpose : Send multiple concurrent patients to test system capacity.
# Usage   : ./scripts/stress_test.sh [NUM_PATIENTS]

TRIAGE_FIFO="/tmp/hospital_triage.fifo"
PID_FILE="/tmp/hospital_admissions.pid"
num="${1:-20}"

echo "Hospital Stress Test"
echo "--------------------"

if [ "${1:-}" = "-h" ] || [ "${1:-}" = "--help" ]; then
    echo "Usage: ./scripts/stress_test.sh [NUM_PATIENTS]"
    exit 0
fi

if ! [[ "$num" =~ ^[0-9]+$ ]] || [ "$num" -le 0 ]; then
    echo "ERROR: NUM_PATIENTS must be a positive number."
    exit 1
fi

if [ ! -f "$PID_FILE" ] || ! kill -0 "$(cat "$PID_FILE")" 2>/dev/null; then
    echo "ERROR: Hospital is not running. Start with start_hospital.sh"
    exit 1
fi

echo "Sending $num patients concurrently ..."
echo ""

NAMES=("Alice" "Bob" "Carlos" "Diana" "Eve" "Frank" "Grace" "Hina"
       "Ivan" "Julia" "Khalid" "Layla" "Mike" "Nadia" "Omar" "Priya"
       "Qais" "Rita" "Sam" "Tina" "Umar" "Vera" "Waqar" "Xena"
       "Yasir" "Zoha")

SYMPTOMS=("chest pain" "high fever" "breathing difficulty" "fracture" "seizure"
           "severe bleeding" "stroke symptoms" "head trauma" "burn injury")

get_severity() {
    local r
    r=$((RANDOM % 100))
    if   [ "$r" -lt 20 ]; then echo $((9 + RANDOM % 2)) # 9-10
    elif [ "$r" -lt 50 ]; then echo $((5 + RANDOM % 4)) # 5-8
    else echo $((1 + RANDOM % 4)) # 1-4
    fi
}

sent=0
pids=()

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

for i in $(seq 1 "$num"); do
    name="${NAMES[$((RANDOM % ${#NAMES[@]}))]}_${i}"
    age=$((20 + RANDOM % 60))
    sev="$(get_severity)"
    sym="${SYMPTOMS[$((RANDOM % ${#SYMPTOMS[@]}))]}"

    # Use triage script directly to submit patient
    "$SCRIPT_DIR/triage.sh" "$name" "$age" "$sev" "$sym" > /dev/null &
    
    pids+=($!)
    sent=$((sent + 1))
    sleep 0.1
done

for pid in "${pids[@]}"; do
    wait "$pid" 2>/dev/null
done

echo ""
echo "OK: Sent $sent patients."
echo "Monitor: tail -f logs/admissions.log"
sleep 2
echo "Active simulators:"
pgrep -a patient_simulator 2>/dev/null || echo "  (none — all discharged)"
