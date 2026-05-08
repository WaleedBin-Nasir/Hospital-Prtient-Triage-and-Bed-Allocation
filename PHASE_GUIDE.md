# Hospital Admissions Manager — Phase Guide

## 📋 Overview
This document explains all execution phases in the Hospital Patient Triage & Bed Allocator system. Each phase is clearly marked with highlighted comments in the source code.

---

## 🏥 **ADMISSIONS MANAGER** (`src/admissions_manager.c`)

### **PHASE 1 — INITIALIZATION & DATA STRUCTURES**
**Location:** Top of file after headers  
**Purpose:** Define global variables, data structures, and utilities needed by the entire system

**Key Components:**
- Allocation strategy selection (BEST_FIT, FIRST_FIT, WORST_FIT)
- Patient metrics tracking array for Gantt chart generation
- Priority queue (min-heap) for patient triage (Receptionist → Scheduler)
- ANSI color codes and logging utilities
- Bounded buffer semaphores for producer-consumer pattern

**Status:** ✓ Highlighted with double-line box separator

---

### **PHASE 2 — IPC & PROCESS MANAGEMENT**
**Location:** Middle section of file  
**Purpose:** Handle Inter-Process Communication, FIFO parsing, and spawning patient simulator processes

**Key Components:**
- **Shared Memory Setup** (`shm_create`, `shm_destroy`)
  - Creates POSIX shared memory for BedMap
  - Initializes process-shared mutexes and condition variables
  - Sets up initial bed partitions for each ward (ICU, Isolation, General)
  - Initializes page table for memory management

- **TRIAGE FIFO Parsing** (`parse_record`)
  - Reads patient data from TRIAGE_FIFO
  - Format: `patient_id|name|age|severity|priority|care_units|symptoms`

- **Process Spawning** (`spawn_simulator`)
  - Forks and executes patient_simulator child processes
  - Passes patient data via command-line arguments

**Status:** ✓ Highlighted with clear phase header

---

### **PHASE 3 — THREAD ROUTINES & SYNCHRONIZATION**
**Location:** Large middle section of file (longest phase)  
**Purpose:** Implement concurrent thread operations using semaphores, mutexes, and condition variables

**Three Thread Types:**

#### **3a. Receptionist Thread** 
- **Role:** Producer in bounded-buffer pattern
- **Operation:** Continuously reads from TRIAGE_FIFO
- **Output:** Pushes PatientRecord into priority queue (sorted by priority)
- **Synchronization:** Uses semaphores (`slots_free`) to block when queue is full

#### **3b. Scheduler Thread**
- **Role:** Consumer from queue, Allocator of beds
- **Operation:** 
  1. Pop patient from priority queue
  2. Acquire ward-specific semaphore (ICU/Isolation/General)
  3. Allocate bed using configured strategy
  4. Spawn patient_simulator process
  5. Track patient metrics (arrival, admission time)
- **Synchronization:** Semaphores, mutexes, and condition variables for bed allocation

#### **3c. Nurse Threads** (3 instances, one per ward)
- **Role:** Monitor patient discharges from SIM_FIFO
- **Operation:**
  1. Read discharge notifications from SIM_FIFO
  2. Free bed partition and coalesce adjacent free blocks
  3. Release ward semaphore for next patient
  4. Update patient completion metrics
- **Synchronization:** Mutex protection for shared bed partition access

#### **3d. Scheduling Simulation**
- **Purpose:** Generate comparative analysis of scheduling algorithms
- **Algorithms Simulated:**
  - **FCFS** (First-Come-First-Served) — sorted by arrival time
  - **SJF** (Shortest Job First) — sorted by care units
  - **Priority** (Actual system) — sorted by priority level
- **Output:** Written to `schedule_log.txt` with averages

**Status:** ✓ Highlighted with sub-phase markers for each thread

---

### **PHASE 4 — MEMORY MANAGEMENT**
**Location:** Memory utilities section (before threads)  
**Purpose:** Implement bed allocation, deallocation, paging, and fragmentation tracking

**Key Functions:**

#### **4a. Memory Utilities**
- `append_mmap_record()` — mmap-based patient record logging

#### **4b. Paging System**
- `page_table_init()` — Initialize page table with -1 (free)
- `page_table_allocate()` — Mark pages as used, calculate internal fragmentation
- `page_table_free()` — Free pages and update table
- `page_table_print()` — Display current page allocation state

#### **4c. Allocation Strategies**
- `find_partition()` — Supports BEST_FIT, FIRST_FIT, WORST_FIT
- `allocate_bed()` — Allocate and split partitions as needed
- `free_bed()` — Deallocate and coalesce adjacent free blocks

#### **4d. Fragmentation Analysis**
- `report_fragmentation()` — Calculate external fragmentation percentage
- Tracks: free units, largest contiguous block, fragmentation ratio

#### **4e. Visualization**
- `print_ward_map()` — Display ward state before/after coalescing

**Status:** ✓ Highlighted with phase 4 header

---

### **PHASE 5 — SIGNAL HANDLING**
**Location:** Before main() function  
**Purpose:** Handle process signals for graceful shutdown and runtime strategy changes

**Signal Handlers:**

- **SIGCHLD** — Clean up zombie child processes
  - Called when patient_simulator process terminates
  - Uses `waitpid()` with WNOHANG to reap children

- **SIGTERM / SIGINT** — Graceful shutdown
  - Sets `g_running = 0` to stop main loop
  - Triggers thread cleanup sequence

- **SIGUSR1** — Runtime strategy cycling (Bonus Feature!)
  - Cycles through: BEST_FIT → FIRST_FIT → WORST_FIT → BEST_FIT
  - Command: `kill -USR1 <admissions_manager_pid>`

**Status:** ✓ Highlighted with phase 5 header

---

### **PHASE 6 — MAIN EXECUTION & ORCHESTRATION**
**Location:** `main()` function  
**Purpose:** Coordinate all system initialization, execution, and graceful shutdown

#### **PHASE 6.1 — CLI PARSING & INITIALIZATION**
- Parse `--strategy` flag for allocation algorithm selection
- Print startup banner with system configuration
- Open log file for file-based logging

#### **PHASE 6.2 — SETUP & THREAD CREATION**
- Create FIFOs (TRIAGE_FIFO, SIM_FIFO)
- Create shared memory (BedMap)
- Initialize queue
- Register signal handlers
- Spawn 5 threads:
  - 1 Receptionist
  - 1 Scheduler
  - 3 Nurses (ICU, Isolation, General)

#### **PHASE 6.3 — MAIN EVENT LOOP**
- `while (g_running) { pause(); }` — Wait for signals
- System runs entirely through thread operations and IPC

#### **PHASE 6.4 — GRACEFUL SHUTDOWN**
- Cancel all threads
- Unblock threads waiting on FIFOs
- Signal semaphores and condition variables
- Join all threads

#### **PHASE 6.5 — ANALYTICS & REPORTING**
- Generate Gantt chart with scheduling analysis
- Write metrics to `schedule_log.txt`
- Print shutdown summary with statistics
- Clean up resources (shared memory, FIFOs)

**Status:** ✓ Highlighted with numbered sub-phase headers (6.1–6.5)

---

## 👤 **PATIENT SIMULATOR** (`src/patient_simulator.c`)

### **PHASE 1 — INITIALIZATION & INCLUDES**
**Location:** Top of file  
**Purpose:** Set up includes and system configuration

**Status:** ✓ Highlighted with phase 1 header

---

### **PHASE 2 — UTILITY FUNCTIONS**
**Location:** Before `main()`  
**Purpose:** Provide helper functions for treatment simulation

**Functions:**
- `treatment_duration(int priority)` — Calculate random treatment time
  - ICU (priority 1-2): 5-15 seconds
  - Isolation (priority 3): 3-10 seconds
  - General (priority 4-5): 2-8 seconds
  - Uses PID-based seed for per-process randomness

- `log_simulator()` — Log patient admission and treatment start

**Status:** ✓ Highlighted with phase 2 header

---

### **PHASE 3 — MAIN EXECUTION**
**Location:** `main()` function  
**Purpose:** Execute patient treatment simulation and report discharge

#### **PHASE 3.1 — Parse & Validate Arguments**
```bash
./patient_simulator <id> <name> <severity> <priority> <bed>
```
Validate priority is in range [1, 5]

#### **PHASE 3.2 — Calculate Treatment Duration**
Generate random duration based on priority/bed type

#### **PHASE 3.3 — Log Arrival & Start**
Print patient admission with timestamp

#### **PHASE 3.4 — Simulate Treatment**
Sleep for calculated duration

#### **PHASE 3.5 — Report Discharge**
Write discharge notification to SIM_FIFO format: `<id>|<bed>|DISCHARGE\n`

#### **PHASE 3.6 — Log Discharge Confirmation**
Print completion message and exit

**Status:** ✓ Highlighted with numbered sub-phase markers (3.1–3.6)

---

## 📊 System Flow Summary

```
┌─────────────────────────────────────────────────────────────────┐
│                    ADMISSIONS MANAGER                           │
│  ┌──────────────┐  ┌──────────────┐  ┌──────────────────────┐  │
│  │  PHASE 1     │  │  PHASE 2     │  │  PHASE 3             │  │
│  │              │  │              │  │                      │  │
│  │ Init Data    │→ │ IPC Setup    │→ │ Thread Operations    │  │
│  │ Structures   │  │ Shared Mem   │  │ - Receptionist ──┐   │  │
│  │              │  │ FIFO Creation│  │ - Scheduler      │   │  │
│  │              │  │              │  │ - Nurses ←───────┘   │  │
│  └──────────────┘  └──────────────┘  │                      │  │
│                                       │ PHASE 4: Memory Mgmt │  │
│                                       │ Run concurrently     │  │
│                                       └──────────────────────┘  │
│                                                                 │
│  ┌──────────────┐  ┌──────────────┐  ┌──────────────────────┐  │
│  │  PHASE 5     │  │  PHASE 6     │  │  PHASE 6.3→6.5      │  │
│  │              │  │              │  │                      │  │
│  │ Signal       │  │ Main Loop    │→ │ Shutdown, Analytics │  │
│  │ Handlers     │  │ & Threads    │  │ Report Results      │  │
│  │ SIGCHLD      │  │ CLI Parsing  │  │                      │  │
│  │ SIGTERM      │  │              │  │                      │  │
│  │ SIGUSR1      │  │              │  │                      │  │
│  └──────────────┘  └──────────────┘  └──────────────────────┘  │
└─────────────────────────────────────────────────────────────────┘
                            ↕ (IPC)
┌─────────────────────────────────────────────────────────────────┐
│             PATIENT SIMULATOR (Forked Children)                 │
│  ┌──────────┐  ┌──────────┐  ┌──────────────────────┐           │
│  │ PHASE 1  │  │ PHASE 2  │  │ PHASE 3              │           │
│  │          │  │          │  │                      │           │
│  │ Init     │→ │ Utilities│→ │ Treatment Simulation │           │
│  │ Includes │  │ Functions│  │ & Discharge Report   │           │
│  └──────────┘  └──────────┘  └──────────────────────┘           │
└─────────────────────────────────────────────────────────────────┘
```

---

## 🔍 Quick Reference: Where to Find Each Phase

| Phase | File | Line Region | Key Feature |
|-------|------|-------------|-------------|
|  1    | admissions_manager.c | ~40-170 | Global vars, data structures |
|  2    | admissions_manager.c | ~520-730 | IPC, shared memory, process spawn |
|  3    | admissions_manager.c | ~730-1040 | Thread routines (Receptionist, Scheduler, Nurses) |
|  4    | admissions_manager.c | ~240-520 | Bed allocation, paging, fragmentation |
|  5    | admissions_manager.c | ~620-690 | Signal handlers |
|  6    | admissions_manager.c | ~950-1200+ | main() orchestration |
| —     | patient_simulator.c | ~30-50 | PHASE 1 & 2 |
| —     | patient_simulator.c | ~60-140 | PHASE 3 main execution |

---

## 🎯 Key Synchronization Mechanisms by Phase

| Phase | Synchronization Method | Purpose |
|-------|------------------------|---------|
| 3a (Receptionist) | Bounded semaphores | Queue producer limiting |
| 3b (Scheduler) | Mutex + Condition Variable | Bed allocation mutual exclusion |
| 3b (Scheduler) | Ward semaphores | Bed type limiting |
| 3c (Nurses) | Mutex + Condition Variable | Bed deallocation safety |
| 4 (Memory) | Partition coalescing | External fragmentation reduction |

---

## 📈 Output Files Generated

| File | Phase Generated | Content |
|------|-----------------|---------|
| `hospital.log` | 1 & throughout | All system logging |
| `schedule_log.txt` | 6.5 | Gantt chart + scheduling analysis |
| `memory_log.txt` | 4 | Fragmentation metrics |
| `patient_records.dat` | 4, 6.4 | mmap-based patient admission/discharge |

---

**Last Updated:** 2026-05-08  
**Phase Comments:** Fully highlighted with visual separators  
**Status:** ✅ All phases clearly marked
