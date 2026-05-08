#!/usr/bin/env bash
# Project : Hospital Patient Triage & Bed Allocator
# Script  : triage.sh
# Group   : Group XX
# Members :
#   - Muhammad Abdullah Khan (24F-0626)
#   - Muhammad Zunair Haider (24F-590)
#   - Waleed bin Nasir (24F-0516)
# Date    : 2026-05-08
# Purpose : Compute triage priority and pipe patient data to admissions.
# Usage   : ./scripts/triage.sh <name> <age> <severity 1-10> [symptoms]
# triage.sh — Patient intake terminal
# Accepts CLI args, interactive, or piped input.

TRIAGE_FIFO="/tmp/hospital_triage.fifo"
PID_FILE="/tmp/hospital_admissions.pid"

if [ ! -f "$PID_FILE" ] || ! kill -0 "$(cat "$PID_FILE")" 2>/dev/null; then
    echo "ERROR: Admissions Manager is not running."
    echo "Start first: ./scripts/start_hospital.sh"
    exit 1
fi
if [ ! -p "$TRIAGE_FIFO" ]; then
    echo "ERROR: FIFO $TRIAGE_FIFO not found."
    exit 1
fi

register_patient() {
    local name="$1"
    local age="$2"
    local sev="$3"
    local sym="${4:-Not specified}"
    
    if [ -z "$name" ]; then
        echo "Name required."
        return 1
    fi
    if ! [[ "$age" =~ ^[0-9]+$ ]]; then
        echo "Age must be numeric."
        return 1
    fi
    if ! [[ "$sev" =~ ^[0-9]+$ ]] || [ "$sev" -lt 1 ] || [ "$sev" -gt 10 ]; then
        echo "Severity must be 1-10."
        return 1
    fi

    local prio units
    if [ "$sev" -ge 9 ]; then
        prio=1
        units=3
    elif [ "$sev" -ge 7 ]; then
        prio=2
        units=3
    elif [ "$sev" -ge 5 ]; then
        prio=3
        units=2
    elif [ "$sev" -ge 3 ]; then
        prio=4
        units=1
    else
        prio=5
        units=1
    fi
    
    local pid_part
    pid_part=$(( ( $(date +%s%N 2>/dev/null | tail -c 7 || date +%s | tail -c 7) + $$ + RANDOM ) % 999983 + 1 ))
    local record="${pid_part}|${name}|${age}|${sev}|${prio}|${units}|${sym}"
    
    echo "$record" > "$TRIAGE_FIFO"
    echo "[TRIAGE] Registered '$name' (severity=$sev, priority=$prio, units=$units)."
}

if [ "${1:-}" = "-h" ] || [ "${1:-}" = "--help" ]; then
    echo "Usage:"
    echo "  ./scripts/triage.sh <name> <age> <severity 1-10> [symptoms]"
    echo "  ./scripts/triage.sh   (interactive mode)"
    echo "  echo 'Ali|23|7|fever' | ./scripts/triage.sh   (batch mode)"
    exit 0
fi

if [ $# -ge 3 ]; then
    register_patient "$1" "$2" "$3" "$4"
    exit $?
fi

if [ ! -t 0 ]; then
    while IFS='|' read -r name age sev sym; do
        [ -z "$name" ] && continue
        register_patient "$name" "$age" "$sev" "$sym"
    done
    exit 0
fi

echo "Hospital Triage Intake"
echo "----------------------"
echo "Type 'q' as patient name to exit."
echo "Severity guide: 9-10 critical, 7-8 high, 5-6 medium, 1-4 low."

while true; do
    echo ""
    read -rp "Patient name  : " name
    if [ "$name" = "q" ] || [ "$name" = "Q" ]; then
        break
    fi
    read -rp "Patient age   : " age
    read -rp "Severity(1-10): " sev
    read -rp "Symptoms      : " sym
    register_patient "$name" "$age" "$sev" "$sym"
    
    read -rp "Register another patient? [y/N]: " more
    if ! [[ "$more" =~ ^[Yy]$ ]]; then
        break
    fi
done

echo "[TRIAGE] Session ended."