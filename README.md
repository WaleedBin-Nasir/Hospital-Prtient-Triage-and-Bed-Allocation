# Hospital Patient Triage & Bed Allocator

## Overview

**CL2006 Operating Systems Lab — Semester Project (Spring 2026)**
**Student**: Project Team

A system-level C simulation of hospital emergency-room operations exercising core OS concepts: **process management**, **inter-process communication (IPC)**, **CPU scheduling**, **thread synchronization**, and **memory allocation**.

Patients arrive at an ER, are triaged (assigned a priority 1–5), and allocated to an appropriate bed (ICU, Isolation, or General Ward). The system handles concurrent arrivals, enforces priority-based scheduling, prevents race conditions on shared resources, and manages bed memory using **Best-Fit**, **First-Fit**, or **Worst-Fit** allocation strategies.

---

## Architecture

| Component | Description |
|---|---|
| **Admissions Manager** (`admissions_manager`) | Central daemon — priority queue, thread pool, bed allocator, fork/exec of patient simulators |
| **Patient Simulator** (`patient_simulator`) | Forked per patient via `fork()+execv()` — sleeps for randomised treatment, then signals discharge via FIFO |
| **Triage Terminal** (`triage.sh`) | Bash frontend — validates input, computes triage priority, pipes record to admissions via FIFO |
| **Start/Stop Scripts** | `start_hospital.sh` bootstraps IPC resources; `stop_hospital.sh` gracefully shuts down and prints summary |

---

## OS Concepts Demonstrated

| Concept | Implementation |
|---|---|
| Shell Scripting | Triage script, start/stop scripts, stress test, Makefile |
| Process Management | `fork()` + `execv()` per patient; `waitpid(WNOHANG)`; SIGCHLD handler |
| IPC — Pipes | Triage record transmitted from `triage.sh` to admissions over IPC stream |
| IPC — Named FIFO | Discharge notification from patient_simulator → admissions |
| IPC — Shared Memory | POSIX `shm_open` bed bitmap shared between admissions and patient processes |
| CPU Scheduling | Priority queue (sorted insertion); FCFS, SJF, Priority simulations logged |
| POSIX Threads | Receptionist, Scheduler, 3× Nurse threads (one per ward type) |
| Mutex / Cond Vars | `pthread_mutex_t` protects bed bitmap; `pthread_cond_t` signals bed-freed events |
| Semaphores | Counting semaphores enforce ICU/Isolation/General capacity limits; bounded-buffer producer-consumer queue |
| Memory Management | Best-Fit / First-Fit / Worst-Fit allocator with partition splitting and coalescing |
| Fragmentation | External fragmentation % computed and logged to `memory_log.txt` |
| Paging Simulation | Page table array (page_size=2) with internal fragmentation per patient |
| Virtual Memory | `mmap()`-based patient record log (`patient_records.dat`) with `msync()`/`munmap()` |

---

## How to Build & Run

### Prerequisites
- Linux (tested on Ubuntu 22.04+)
- GCC with `-pthread` support
- Standard POSIX libraries (`-lrt`)

### 1. Compile
```bash
cd hospital/
make clean && make all
```
By default, compiles with **Best-Fit** allocator. To change at compile time:
```bash
make all ALLOC=FIRST_FIT   # or WORST_FIT
```

### 2. Start the Hospital (Background)
```bash
./scripts/start_hospital.sh          # defaults to BEST_FIT
./scripts/start_hospital.sh FIRST_FIT  # or specify strategy
```
Or use the Makefile shortcut:
```bash
make run
```

### 3. Runtime Strategy Selection
The admissions manager also accepts a `--strategy` flag at runtime:
```bash
./admissions_manager --strategy first &   # first, best, or worst
```

### 4. Admit Patients

**Automated Stress Test** (20 concurrent patients):
```bash
./scripts/stress_test.sh 20
```

**Interactive Entry**:
```bash
./scripts/triage.sh
```

**Single Patient** (CLI):
```bash
./scripts/triage.sh "John Doe" 45 8
```

### 5. Monitor & Shutdown
```bash
tail -f logs/admissions.log          # live logs
./scripts/stop_hospital.sh          # graceful shutdown + summary
```

---

## Project Structure

```
hospital/
├── Makefile                    # all, clean, run, test targets
├── README.md
├── include/
│   ├── bed.h                  # BedPartition, BedMap, page table, constants
│   ├── ipc.h                  # FIFO paths, wire formats
│   └── patient.h              # PatientRecord struct, priority_ward()
├── src/
│   ├── admissions_manager.c   # Core: threads, scheduler, allocator, IPC
│   └── patient_simulator.c    # Forked process: treatment + discharge
├── scripts/
│   ├── triage.sh              # Patient intake terminal
│   ├── start_hospital.sh      # Bootstrap IPC + launch daemon
│   ├── stop_hospital.sh       # Graceful shutdown + cleanup
│   └── stress_test.sh         # 20-patient concurrent stress test
├── logs/
│   └── admissions.log         # Runtime log
├── schedule_log.txt           # Gantt chart + FCFS / SJF metrics
├── memory_log.txt             # Fragmentation statistics
└── patient_records.dat        # mmap-based patient record log
```

---

## Output Files

- **`schedule_log.txt`** — Gantt-style scheduling output with FCFS, SJF, and Priority simulations; average waiting & turnaround times
- **`memory_log.txt`** — External fragmentation percentages per allocation/deallocation event
- **`patient_records.dat`** — mmap-based admission/discharge records
- **`logs/admissions.log`** — Full system log (ward maps, page tables, semaphore events)
