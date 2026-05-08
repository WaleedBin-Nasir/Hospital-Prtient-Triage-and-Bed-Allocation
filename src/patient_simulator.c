/******************************************************************************
 * Project: Hospital Patient Triage & Bed Allocator
 * File: patient_simulator.c
 * Group: Group XX
 * Members: Muhammad Abdullah khan, Muhammad Zunair Haider, Waleed bin Nasir
 * Roll No.: 24F-0626,24F-590,24F-0516
 * Date: 2026-05-08
 ******************************************************************************/
/*
 * Spawned by admissions_manager via fork()+execv().
 * Simulates patient treatment time based on bed type with randomised
 * durations, then reports discharge to admissions via SIM_FIFO.
 *
 * Arguments passed by admissions_manager:
 *   patient_id name severity priority bed_index
 */

/* ════════════════════════════════════════════════════════════════════════════
   ║       PATIENT SIMULATOR — CHILD PROCESS FOR TREATMENT SIMULATION        ║
   ║        Processing individual patients through their hospital admission  ║
   ════════════════════════════════════════════════════════════════════════════ */

/* ────────────────────────────────────────────────────────────────────────────
   ║         PHASE 1 — INITIALIZATION & INCLUDES                            ║
   ──────────────────────────────────────────────────────────────────────────── */

#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>

#include "../include/ipc.h"
#include "../include/patient.h"

/* ────────────────────────────────────────────────────────────────────────────
   ║        PHASE 2 — UTILITY FUNCTIONS                                     ║
   ║     Treatment Duration Calculation & Logging                           ║
   ──────────────────────────────────────────────────────────────────────────── */

/* Random treatment duration per bed type as required by the manual:
 *   ICU       (priority 1-2) : 5 – 15 s
 *   Isolation (priority 3)   : 3 – 10 s
 *   General   (priority 4-5) : 2 –  8 s
 */
static unsigned int treatment_duration(int priority) {
    /* Seed with PID + time for per-process randomness */
    srand((unsigned)(getpid() ^ time(NULL)));

    if (priority <= 2) {           /* ICU */
        return 5 + (rand() % 11);  /* 5..15 */
    } else if (priority == 3) {    /* Isolation */
        return 3 + (rand() % 8);   /* 3..10 */
    } else {                       /* General */
        return 2 + (rand() % 7);   /* 2..8 */
    }
}

static void log_simulator(int pid_ext, const char *name, int prio, int bed, unsigned int dur) {
    char timestamp[32];
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", t);

    printf("[%s] [PID=%d] Patient '%s' (ID=%d, Prio=%d) admitted to bed index %d. "
           "Starting treatment for %us...\n",
           timestamp, getpid(), name, pid_ext, prio, bed, dur);
    fflush(stdout);
}

int main(int argc, char *argv[])
{
    /* ╔════════════════════════════════════════════════════════════════════╗
         ║ PHASE 3 — MAIN EXECUTION                                       ║
         ║   Parse arguments, simulate treatment, report discharge         ║
         ╚════════════════════════════════════════════════════════════════════╝ */
    
    /* ── PHASE 3.1: Parse and validate command-line arguments ── */
    if (argc < 6) {
        fprintf(stderr, "patient_simulator: usage: %s <id> <name> <severity> <priority> <bed>\n", argv[0]);
        return EXIT_FAILURE;
    }
    const char *id_str   = argv[1];
    const char *name     = argv[2];
    const char *sev_str  = argv[3];
    const char *prio_str = argv[4];
    const char *bed_str  = argv[5];

    int           pat_id   = atoi(id_str);
    int           severity = atoi(sev_str);
    int           priority = atoi(prio_str);
    int           bed      = atoi(bed_str);

    (void)severity;

    if (priority < 1 || priority > 5) {
        fprintf(stderr, "patient_simulator: invalid priority %d\n", priority);
        return EXIT_FAILURE;
    }

    /* ── PHASE 3.2: Calculate random treatment duration ── */
    unsigned int dur = treatment_duration(priority);

    /* ── PHASE 3.3: Log patient arrival and treatment start ── */
    log_simulator(pat_id, name, priority, bed, dur);

    /* ── PHASE 3.4: Simulate treatment by sleeping ── */
    sleep(dur);

    /* ── PHASE 3.5: Report discharge back to admissions manager ── */
    int fifo_fd = open(SIM_FIFO, O_WRONLY);
    if (fifo_fd < 0) {
        perror("patient_simulator: open SIM_FIFO");
        return EXIT_FAILURE;
    }

    char result[MAX_LINE];
    snprintf(result, sizeof(result), "%d|%d|DISCHARGE\n", pat_id, bed);

    ssize_t n = write(fifo_fd, result, strlen(result));
    if (n < 0) {
        perror("patient_simulator: write SIM_FIFO");
        close(fifo_fd);
        return EXIT_FAILURE;
    }
    close(fifo_fd);

    /* ── PHASE 3.6: Log discharge confirmation ── */
    printf("[PID=%d] Patient '%s' treatment complete (%us). Bed %d now free.\n",
           getpid(), name, dur, bed);
    fflush(stdout);

    return EXIT_SUCCESS;
}
