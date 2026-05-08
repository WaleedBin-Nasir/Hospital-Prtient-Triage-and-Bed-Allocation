/******************************************************************************
 * Project: Hospital Patient Triage & Bed Allocator
 * File: admissions_manager.c
 * Group: Group XX
 * Members: Muhammad Abdullah khan, Muhammad Zunair Haider, Waleed bin Nasir
 * Roll No.: 24F-0626,24F-590,24F-0516
 * Date: 2026-05-08
 *
 * Purpose: Central admissions manager — process spawning, IPC, thread pool,
 *          scheduling, and bed allocation.
 * Compile: gcc -Wall -Wextra -o admissions_manager admissions_manager.c -lpthread -lrt
 ******************************************************************************/
#define _POSIX_C_SOURCE 200809L
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include <time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/mman.h>
#include <pthread.h>
#include <semaphore.h>
#include <limits.h>
#include <stdarg.h>
#include "../include/patient.h"
#include "../include/bed.h"
#include "../include/ipc.h"
// Module 1 starts
// Subpart: Shared Data Structures and Utilities starts
/* ────────────────────────────────────────────────────────────
 * Runtime allocation strategy (default BEST_FIT, overridden
 * via --strategy best|first|worst CLI flag)
 * ──────────────────────────────────────────────────────────── */
static AllocStrategy g_alloc_strategy = BEST_FIT;
/* ────────────────────────────────────────────────────────────
 * Metrics & Gantt Tracking
 * ──────────────────────────────────────────────────────────── */
typedef struct
{
    int patient_id;
    char name[64];
    int priority;
    int care_units;       // burst proxy for SJF
    time_t arrival;
    time_t admit;
    time_t complete;
} PatientMetrics;
static PatientMetrics g_metrics[200];
static int g_metrics_count = 0;
static pthread_mutex_t g_metrics_lock = PTHREAD_MUTEX_INITIALIZER;
#define GANTT_LOG "schedule_log.txt"
#define MEMORY_LOG "memory_log.txt"
/* ────────────────────────────────────────────────────────────
 * Min-Heap Priority Queue (Receptionist → Scheduler)
 * ──────────────────────────────────────────────────────────── */
#define QUEUE_MAX 64
typedef struct 
{
    PatientRecord items[QUEUE_MAX];
    int count;
    pthread_mutex_t lock;
    pthread_cond_t  ready;
    sem_t           slots_free;   // bounded-buffer: free slots 
    sem_t           slots_used;   // bounded-buffer: used slots
} PatientQueue;
static PatientQueue g_queue;
static void queue_init(PatientQueue *q) 
{
    q->count = 0;
    pthread_mutex_init(&q->lock, NULL);
    pthread_cond_init(&q->ready, NULL);
    sem_init(&q->slots_free, 0, QUEUE_MAX);
    sem_init(&q->slots_used, 0, 0);
}
//Producer-consumer push via bounded semaphore 
static void queue_push(PatientQueue *q, const PatientRecord *rec) 
{
    sem_wait(&q->slots_free);   //block if queue full
    pthread_mutex_lock(&q->lock);
    //Sorted insertion: smallest priority first
    int i = q->count - 1;
    while (i >= 0 && q->items[i].priority > rec->priority) 
    {
        q->items[i+1] = q->items[i];
        i--;
    }
    q->items[i+1] = *rec;
    q->count++;
    pthread_cond_signal(&q->ready);
    pthread_mutex_unlock(&q->lock);
    sem_post(&q->slots_used);   // signal consumer 
}

static int queue_pop(PatientQueue *q, PatientRecord *out) 
{
    sem_wait(&q->slots_used);   // block if queue empty 

    pthread_mutex_lock(&q->lock);
    if (q->count == 0)              // woken for shutdown, not a real item 
    {        
        pthread_mutex_unlock(&q->lock);
        return 0;

    }
    *out = q->items[0];
    for (int i = 1; i < q->count; i++) 
    {
        q->items[i-1] = q->items[i];
    }
    q->count--;
    pthread_mutex_unlock(&q->lock);
    sem_post(&q->slots_free);   // signal producer 
    return 1;
}
// Subpart: Bounded Buffer Queue (Synchronization) ends
// Subpart: Logging Utilities starts
static BedMap *g_bedmap = NULL;
// Subpart: Shared Data Structures and Utilities ends
// Module: Phase 3 - Threads & Synchronization starts
// Subpart: Bounded Buffer Queue (Synchronization) starts
static FILE *g_log_fp = NULL;

//ANSI colour codes for prettier terminal output 
#define CLR_RESET  "\033[0m"
#define CLR_BOLD   "\033[1m"
#define CLR_RED    "\033[0;31m"
#define CLR_GRN    "\033[0;32m"
#define CLR_YLW    "\033[1;33m"
#define CLR_BLU    "\033[0;34m"
#define CLR_MAG    "\033[0;35m"
#define CLR_CYN    "\033[0;36m"
#define CLR_DIM    "\033[2m"
static const char *tag_colour(const char *msg) {
    if (strstr(msg, "[RECEPTIONIST]")) 
    { 
        return CLR_CYN; 
    }
    if (strstr(msg, "[SCHEDULER]"))    
    { 
        return CLR_BLU;
    }
    if (strstr(msg, "[NURSE"))         
    { 
        return CLR_MAG; 
    }
    if (strstr(msg, "[SEMAPHORE]"))    
    { 
        return CLR_YLW; 
    }
    if (strstr(msg, "[STRATEGY]"))     
    {
        return CLR_GRN; 
    }
    if (strstr(msg, "[PAGING]"))       
    {
        return CLR_DIM; 
    }
    if (strstr(msg, "[FRAG]"))         
    {
        return CLR_DIM; 
    }
    if (strstr(msg, "[PAGE TABLE]"))   
    { 
        return CLR_DIM; 
    }
    if (strstr(msg, "ALLOC_COMPARE"))  
    { return CLR_DIM; }
    if (strstr(msg, "WARD MAP"))       
    { 
        return CLR_DIM; 
    }
    if (strstr(msg, "==="))            
    { 
        return CLR_GRN; 
    }
    return CLR_RESET;
}
static void write_log(const char *msg) 
{
    char timestamp[32];
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    strftime(timestamp, sizeof(timestamp), "%H:%M:%S", t);
    // File log — no colours, full timestamp
    if (g_log_fp) 
    {
        char full_ts[32];
        strftime(full_ts, sizeof(full_ts), "%Y-%m-%d %H:%M:%S", t);
        fprintf(g_log_fp, "[%s] %s\n", full_ts, msg);
        fflush(g_log_fp);
    }
    // Console — coloured, short timestamp
    const char *clr = tag_colour(msg);
    printf("%s[%s]%s %s%s%s\n", CLR_DIM, timestamp, CLR_RESET, clr, msg, CLR_RESET);
    fflush(stdout);
}
static void log_admission(const char *event, int bed, const char *fmt, ...) 
{
    char body[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(body, sizeof(body), fmt, ap);
    va_end(ap);
    char msg[768];
    snprintf(msg, sizeof(msg), "[%s] bed=%d  %s", event, bed, body);
    write_log(msg);
}
// Subpart: Logging Utilities ends
// Module 3
// Subpart: Memory Utilities starts
/* ────────────────────────────────────────────────────────────
 * Phase 4 — Allocator, Paging, Fragmentation
 * ──────────────────────────────────────────────────────────── */
static void print_ward_map(BedMap *bm, const char *prefix) 
{
    char buf[1024] = {0};
    int len = 0;
    for (int i = 0; i < bm->partition_count; i++) 
    {
        BedPartition *p = &bm->partitions[i];
        const char *status = p->is_free ? "FREE" : "OCC";
        len += snprintf(buf+len, sizeof(buf)-(size_t)len,"[P%d:%s:%s(sz:%d)] ", p->partition_id, p->bed_type, status, p->size);
    }

    char msg[2048];
    snprintf(msg, sizeof(msg), "%s WARD MAP: %s", prefix, buf);
    write_log(msg);
}
//mmap-based patient record log (Phase 4 bonus) 
static void append_mmap_record(const PatientRecord *rec, const char *action) 
{
    int fd = open("patient_records.dat", O_RDWR | O_CREAT, 0660);
    if (fd < 0)
    {
        return;
    } 
    off_t size = lseek(fd, 0, SEEK_END);
    char entry[512];
    snprintf(entry, sizeof(entry), "%s | ID: %d | Name: %s | Units: %d | Time: %ld\n",action, rec->patient_id, rec->name, rec->care_units, time(NULL));
    int entry_len = (int)strlen(entry);
    if (ftruncate(fd, size + entry_len) < 0) 
    { 
        close(fd); return; 
    }
    char *map = mmap(NULL, (size_t)(size + entry_len), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (map != MAP_FAILED) 
    {
        memcpy(map + size, entry, (size_t)entry_len);
        msync(map, (size_t)(size + entry_len), MS_SYNC);
        munmap(map, (size_t)(size + entry_len));
    }
    close(fd);
}
// Subpart: Memory Utilities ends
// Subpart: Paging starts
// ── Page table helpers ───────────────────────────────────────
static void page_table_init(BedMap *bm) 
{
    for (int i = 0; i < TOTAL_PAGES; i++) 
    {
        bm->page_table[i] = -1;   // all pages free 
    }
}
static void page_table_allocate(BedMap *bm, int start_unit, int size, int patient_id) 
{
    int start_page = start_unit / PAGE_SIZE;
    int pages_needed = (size + PAGE_SIZE - 1) / PAGE_SIZE;
    for (int i = start_page; i < start_page + pages_needed && i < TOTAL_PAGES; i++) 
    {
        bm->page_table[i] = patient_id;
    }

    int wasted = (size % PAGE_SIZE == 0) ? 0 : PAGE_SIZE - (size % PAGE_SIZE);
    char msg[256];
    snprintf(msg, sizeof(msg),"[PAGING] Patient %d: %d units → %d pages (page_size=%d). Internal Fragmentation: %d wasted unit(s)",patient_id, size, pages_needed, PAGE_SIZE, wasted);
    write_log(msg);
}
static void page_table_free(BedMap *bm, int start_unit, int size) 
{
    int start_page = start_unit / PAGE_SIZE;
    int pages = (size + PAGE_SIZE - 1) / PAGE_SIZE;
    for (int i = start_page; i < start_page + pages && i < TOTAL_PAGES; i++) 
    {
        bm->page_table[i] = -1;
    }
}
static void page_table_print(BedMap *bm) 
{
    char buf[512] = {0};
    int len = 0;
    for (int i = 0; i < TOTAL_PAGES && len < 480; i++) 
    {
        len += snprintf(buf+len, sizeof(buf)-(size_t)len, "%d ", bm->page_table[i]);
    }
    char msg[768];
    snprintf(msg, sizeof(msg), "[PAGE TABLE] %s", buf);
    write_log(msg);
}
// Subpart: Paging ends
// Subpart: Fragmentation Reporting starts
// ── Fragmentation reporting ────────────────────────────────── 
static void report_fragmentation(BedMap *bm, const char *ward_type) 
{
    int free_units = 0;
    int largest_run = 0;
    for (int i = 0; i < bm->partition_count; i++) 
    {
        if (strcmp(bm->partitions[i].bed_type, ward_type) == 0) 
        {
            if (bm->partitions[i].is_free) 
            {
                free_units += bm->partitions[i].size;
                if (bm->partitions[i].size > largest_run)
                {
                    largest_run = bm->partitions[i].size;
                }
                    
            }
        }
    }
    double frag_pct = 0.0;
    if (free_units > 0 && largest_run < free_units) 
    {
        frag_pct = (1.0 - ((double)largest_run / (double)free_units)) * 100.0;
    }

    char msg[256];
    snprintf(msg, sizeof(msg),"[FRAG] Ward=%-10s FreeUnits=%d LargestFreeBlock=%d ExtFragmentation=%.2f%%", ward_type, free_units, largest_run, frag_pct);
    write_log(msg);
    FILE *f = fopen(MEMORY_LOG, "a");
    if (f) 
    {
        time_t now = time(NULL);
        fprintf(f, "[%ld] %s\n", now, msg);
        fclose(f);
    }
}
// Subpart: Fragmentation Reporting ends
// Subpart: Bed Allocation and Deallocation starts
// ── Partition finder (supports all 3 strategies) ───────────── 
static int find_partition(BedMap *bm, const char *ward_type, int units, AllocStrategy strategy) 
{
    int best_index = -1;
    int best_size = INT_MAX;
    int worst_size = -1;

    for (int i = 0; i < bm->partition_count; i++)
    {
        if (bm->partitions[i].is_free &&bm->partitions[i].size >= units &&strcmp(bm->partitions[i].bed_type, ward_type) == 0) 
        {
            if (strategy == FIRST_FIT) 
            {
                return i;
            } 
            else if (strategy == BEST_FIT) 
            {
                if (bm->partitions[i].size < best_size) 
                {
                    best_size = bm->partitions[i].size;
                    best_index = i;
                }
            } 
            else if (strategy == WORST_FIT) 
            {
                if (bm->partitions[i].size > worst_size) 
                {
                    worst_size = bm->partitions[i].size;
                    best_index = i;
                }
            }
        }
    }
    return best_index;
}

// ── Allocate a bed partition ───────────────────────────────── 
static int allocate_bed(BedMap *bm, PatientRecord *rec) {
    const char *target = priority_ward(rec->priority);
    int p_idx = find_partition(bm, target, rec->care_units, g_alloc_strategy);
    // Log comparison of all 3 strategies 
    int ff = find_partition(bm, target, rec->care_units, FIRST_FIT);
    int bf = find_partition(bm, target, rec->care_units, BEST_FIT);
    int wf = find_partition(bm, target, rec->care_units, WORST_FIT);
    log_admission("ALLOC_COMPARE", -1,"Ward=%s FirstFitIdx=%d BestFitIdx=%d WorstFitIdx=%d ChosenIdx=%d Strategy=%d",target, ff, bf, wf, p_idx, (int)g_alloc_strategy);
    if (p_idx == -1) 
    {
        return -1;
    }
    // Split if partition is larger than needed — only if array has room 
    if (bm->partitions[p_idx].size > rec->care_units &&bm->partition_count < MAX_PARTITIONS) {
        for (int i = bm->partition_count; i > p_idx; i--) 
        {
            bm->partitions[i] = bm->partitions[i-1];
        }
        bm->partition_count++;
        bm->partitions[p_idx+1].partition_id = bm->partitions[p_idx].partition_id + 100;
        bm->partitions[p_idx+1].start_unit = bm->partitions[p_idx].start_unit + rec->care_units;
        bm->partitions[p_idx+1].size = bm->partitions[p_idx].size - rec->care_units;
        bm->partitions[p_idx+1].is_free = 1;
        bm->partitions[p_idx+1].patient_id = -1;
        strcpy(bm->partitions[p_idx+1].bed_type, bm->partitions[p_idx].bed_type);
        bm->partitions[p_idx].size = rec->care_units;
    }
    bm->partitions[p_idx].is_free = 0;
    bm->partitions[p_idx].patient_id = rec->patient_id;
    // Page table allocation 
    page_table_allocate(bm, bm->partitions[p_idx].start_unit, rec->care_units, rec->patient_id);
    page_table_print(bm);
    report_fragmentation(bm, target);
    append_mmap_record(rec, "ADMITTED");
    return p_idx;
}

// ── Free a bed and coalesce ────────────────────────────────── 
static void free_bed(BedMap *bm, int pat_id) 
{
    int target_idx = -1;
    for (int i = 0; i < bm->partition_count; i++) 
    {
        if (bm->partitions[i].patient_id == pat_id) 
        {
            target_idx = i;
            break;
        }
    }
    if (target_idx == -1) 
    {
        return;
    }
    print_ward_map(bm, "BEFORE COALESCE");
    char ward_type[32];
    strcpy(ward_type, bm->partitions[target_idx].bed_type);
    // Free page table entries 
    page_table_free(bm, bm->partitions[target_idx].start_unit,bm->partitions[target_idx].size);
    bm->partitions[target_idx].is_free = 1;
    bm->partitions[target_idx].patient_id = -1;

    ///Coalesce right 
    if (target_idx < bm->partition_count - 1 &&bm->partitions[target_idx+1].is_free &&strcmp(bm->partitions[target_idx+1].bed_type, ward_type) == 0) 
    {
        bm->partitions[target_idx].size += bm->partitions[target_idx+1].size;
        for (int i = target_idx + 1; i < bm->partition_count - 1; i++) 
        {
            bm->partitions[i] = bm->partitions[i+1];
        }
        bm->partition_count--;
    }

    // Coalesce left 
    if (target_idx > 0 &&bm->partitions[target_idx-1].is_free &&strcmp(bm->partitions[target_idx-1].bed_type, ward_type) == 0) 
    {
        bm->partitions[target_idx-1].size += bm->partitions[target_idx].size;
        for (int i = target_idx; i < bm->partition_count - 1; i++) 
        {
            bm->partitions[i] = bm->partitions[i+1];
        }
        bm->partition_count--;
    }

    print_ward_map(bm, "AFTER COALESCE");
    page_table_print(bm);
    report_fragmentation(bm, ward_type);
    pthread_cond_broadcast(&bm->bed_free);
    // mmap discharge record 
    PatientRecord dummy = { .patient_id = pat_id };
    strcpy(dummy.name, "Discharged_Patient");
    dummy.care_units = 0;
    append_mmap_record(&dummy, "DISCHARGED");
}
// Subpart: Bed Allocation and Deallocation ends
// Module: Phase 4 - Memory Management ends
// Subpart: Process Management (Signals) starts
/* ────────────────────────────────────────────────────────────
 * Signal handlers
 * ──────────────────────────────────────────────────────────── */
static volatile sig_atomic_t g_running = 1;

static void sigchld_handler(int sig) {
    (void)sig;
    int saved_errno = errno;
    while (waitpid(-1, NULL, WNOHANG) > 0) {}
    errno = saved_errno;
}

static void sigterm_handler(int sig) {
    (void)sig;
    g_running = 0;
}

// Bonus: Cycle allocation strategy on SIGUSR1
static void sigusr1_handler(int sig) {
    (void)sig;
    // Cycle through strategies: BEST_FIT -> FIRST_FIT -> WORST_FIT -> BEST_FIT
    if (g_alloc_strategy == BEST_FIT) {
        g_alloc_strategy = FIRST_FIT;
        write_log("[STRATEGY] Switched to FIRST_FIT via SIGUSR1");
    } else if (g_alloc_strategy == FIRST_FIT) {
        g_alloc_strategy = WORST_FIT;
        write_log("[STRATEGY] Switched to WORST_FIT via SIGUSR1");
    } else {
        g_alloc_strategy = BEST_FIT;
        write_log("[STRATEGY] Switched to BEST_FIT via SIGUSR1");
    }
}
// Subpart: Process Management (Signals) ends
// Subpart: IPC (Shared Memory) starts
/* ────────────────────────────────────────────────────────────
 * Shared memory setup / teardown
 * ──────────────────────────────────────────────────────────── */
static BedMap *shm_create(void) {
    shm_unlink(SHM_NAME);
    int fd = shm_open(SHM_NAME, O_CREAT | O_RDWR, 0660);
    if (fd < 0) { return NULL; }
    if (ftruncate(fd, sizeof(BedMap)) < 0) {
        close(fd);
        return NULL;
    }
    BedMap *bm = mmap(NULL, sizeof(BedMap), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    close(fd);
    if (bm == MAP_FAILED) { return NULL; }

    memset(bm, 0, sizeof(BedMap));

    pthread_mutexattr_t mattr;
    pthread_mutexattr_init(&mattr);
    pthread_mutexattr_setpshared(&mattr, PTHREAD_PROCESS_SHARED);
    pthread_mutex_init(&bm->lock, &mattr);
    pthread_mutexattr_destroy(&mattr);

    pthread_condattr_t cattr;
    pthread_condattr_init(&cattr);
    pthread_condattr_setpshared(&cattr, PTHREAD_PROCESS_SHARED);
    pthread_cond_init(&bm->bed_free, &cattr);
    pthread_condattr_destroy(&cattr);

    sem_init(&bm->icu_sem, 1, ICU_PARTITIONS);
    sem_init(&bm->iso_sem, 1, ISOLATION_PARTITIONS);
    sem_init(&bm->gen_sem, 1, GENERAL_PARTITIONS);

    // Initial partitions: one contiguous block per ward type
    bm->partition_count = 3;
    bm->partitions[0] = (BedPartition){1, ICU_START,       ICU_PARTITIONS*ICU_UNITS_PER,       1, -1, "ICU"};
    bm->partitions[1] = (BedPartition){2, ISOLATION_START,  ISOLATION_PARTITIONS*ISOLATION_UNITS_PER, 1, -1, "Isolation"};
    bm->partitions[2] = (BedPartition){3, GENERAL_START,    GENERAL_PARTITIONS*GENERAL_UNITS_PER,     1, -1, "General"};

    // Initialise page table
    page_table_init(bm);

    return bm;
}

static void shm_destroy(BedMap *bm) {
    if (!bm) { return; }
    sem_destroy(&bm->icu_sem);
    sem_destroy(&bm->iso_sem);
    sem_destroy(&bm->gen_sem);
    pthread_mutex_destroy(&bm->lock);
    pthread_cond_destroy(&bm->bed_free);
    munmap(bm, sizeof(BedMap));
    shm_unlink(SHM_NAME);
}

// Module: Phase 2 - Process Management / IPC / Scheduling
/* ────────────────────────────────────────────────────────────
 * Parse one TRIAGE_FIFO line
 * Format: patient_id|name|age|severity|priority|care_units|symptoms
 * ──────────────────────────────────────────────────────────── */
static int parse_record(const char *line, PatientRecord *out) {
    memset(out, 0, sizeof(*out));
    out->assigned_bed = -1;
    out->arrival_time = time(NULL);
    char buf[MAX_LINE];
    strncpy(buf, line, sizeof(buf) - 1);
    char *nl = strchr(buf, '\n');
    if (nl) {
        *nl = '\0';
    }

    char *tok = strtok(buf, "|"); if (!tok) return -1; out->patient_id = atoi(tok);
    tok = strtok(NULL, "|"); if (!tok) return -1; strncpy(out->name, tok, sizeof(out->name) - 1);
    tok = strtok(NULL, "|"); if (!tok) return -1; out->age = atoi(tok);
    tok = strtok(NULL, "|"); if (!tok) return -1; out->severity = atoi(tok);
    tok = strtok(NULL, "|"); if (!tok) return -1; out->priority = atoi(tok);
    tok = strtok(NULL, "|"); if (!tok) return -1; out->care_units = atoi(tok);
    tok = strtok(NULL, "|"); if (tok) strncpy(out->symptoms, tok, sizeof(out->symptoms) - 1);

    return 0;
}

static pid_t spawn_simulator(PatientRecord *rec) {
    char id_str[16], sev_str[16], prio_str[16], bed_str[8];
    snprintf(id_str,   sizeof(id_str),   "%d", rec->patient_id);
    snprintf(sev_str,  sizeof(sev_str),  "%d", rec->severity);
    snprintf(prio_str, sizeof(prio_str), "%d", rec->priority);
    snprintf(bed_str,  sizeof(bed_str),  "%d", rec->assigned_bed);

    pid_t pid = fork();
    if (pid == 0) {
        char *args[] = {
            "./patient_simulator",
            id_str,
            rec->name,
            sev_str,
            prio_str,
            bed_str,
            NULL
        };
        execv("./patient_simulator", args);
        _exit(EXIT_FAILURE);
    }
    return pid;
}

static sem_t *ward_sem(BedMap *bm, int priority) {
    if (priority <= 2) return &bm->icu_sem;
    if (priority == 3) return &bm->iso_sem;
    return &bm->gen_sem;
}

/* ────────────────────────────────────────────────────────────
 * Phase 3 — Thread routines
 * ──────────────────────────────────────────────────────────── */

// Module 2
// Subpart: Receptionist Thread starts
/* ── Receptionist Thread ─────────────────────────────────── */
static void *receptionist_thread(void *arg) {
    (void)arg;
    write_log("[RECEPTIONIST] Started — reading from TRIAGE_FIFO.");
    while (g_running) {
        int fd = open(TRIAGE_FIFO, O_RDONLY);
        if (fd < 0) { sleep(1); continue; }
        FILE *fp = fdopen(fd, "r");
        if (!fp) { close(fd); sleep(1); continue; }
        char line[MAX_LINE];
        while (g_running && fgets(line, sizeof(line), fp)) {
            if (line[0] == '\n' || line[0] == '\0') continue;
            PatientRecord rec;
            if (parse_record(line, &rec) < 0) continue;
            write_log("[RECEPTIONIST] Enqueueing patient (producer → bounded semaphore).");
            queue_push(&g_queue, &rec);
        }
        fclose(fp);
    }
    return NULL;
}
// Subpart: Receptionist Thread ends

// Module 2
/* ── Scheduler Thread ────────────────────────────────────── */
static void *scheduler_thread(void *arg) {
    (void)arg;
    write_log("[SCHEDULER] Started — consuming from priority queue.");
    while (g_running) {
        PatientRecord rec;
        if (!queue_pop(&g_queue, &rec)) continue;

        const char *ward_name = priority_ward(rec.priority);
        sem_t *s = ward_sem(g_bedmap, rec.priority);

        // Semaphore blocking/release demonstration
        {
            char msg[256];
            int sem_val = 0;
            sem_getvalue(s, &sem_val);
            if (sem_val <= 0) {
                snprintf(msg, sizeof(msg),
                         "[SEMAPHORE] %s full — patient %d (%s) waiting for slot...",
                         ward_name, rec.patient_id, rec.name);
                write_log(msg);
            }
        }

        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_sec += 10;
        if (sem_timedwait(s, &ts) < 0) {
            if (errno == ETIMEDOUT) {
                char msg[256];
                snprintf(msg, sizeof(msg),
                         "[SEMAPHORE] Timeout waiting for %s slot — re-queuing patient %d",
                         ward_name, rec.patient_id);
                write_log(msg);
                queue_push(&g_queue, &rec);
            }
            continue;
        }

        {
            char msg[256];
            snprintf(msg, sizeof(msg),
                     "[SEMAPHORE] %s slot acquired — admitting patient %d (%s)",
                     ward_name, rec.patient_id, rec.name);
            write_log(msg);
        }

        pthread_mutex_lock(&g_bedmap->lock);
        int bed = allocate_bed(g_bedmap, &rec);

        while (bed < 0 && g_running) {
            // Required condition-variable behavior: wait for bed-free signal.
            pthread_cond_wait(&g_bedmap->bed_free, &g_bedmap->lock);
            bed = allocate_bed(g_bedmap, &rec);
        }
        pthread_mutex_unlock(&g_bedmap->lock);

        if (bed < 0) {
            sem_post(s);
            queue_push(&g_queue, &rec);
            continue;
        }

        rec.assigned_bed = bed;
        pid_t pid = spawn_simulator(&rec);
        if (pid > 0) {
            rec.simulator_pid = pid;
            pthread_mutex_lock(&g_metrics_lock);
            PatientMetrics *m = &g_metrics[g_metrics_count];
            m->patient_id  = rec.patient_id;
            strncpy(m->name, rec.name, sizeof(m->name) - 1);
            m->priority    = rec.priority;
            m->care_units  = rec.care_units;
            m->arrival     = rec.arrival_time;
            m->admit       = time(NULL);
            m->complete    = 0;
            g_metrics_count++;
            pthread_mutex_unlock(&g_metrics_lock);
        }
    }
    return NULL;
}
// Subpart: Scheduler Thread ends

// Module 2
/* ── Nurse Threads (one per bed type) ────────────────────── */
typedef struct {
    const char *ward_name;   // "ICU", "Isolation", "General"
} NurseArg;

static void *nurse_thread(void *arg) {
    NurseArg *na = (NurseArg *)arg;
    char logbuf[256];
    snprintf(logbuf, sizeof(logbuf), "[NURSE-%s] Started — monitoring discharges.", na->ward_name);
    write_log(logbuf);

    while (g_running) {
        int fd = open(SIM_FIFO, O_RDONLY);
        if (fd < 0) { sleep(1); continue; }
        FILE *fp = fdopen(fd, "r");
        if (!fp) { close(fd); sleep(1); continue; }

        char line[MAX_LINE];
        while (g_running && fgets(line, sizeof(line), fp)) {
            if (line[0] == '\n' || line[0] == '\0') continue;
            char tmp[MAX_LINE];
            strncpy(tmp, line, sizeof(tmp)-1);
            tmp[sizeof(tmp)-1] = '\0';
            char *tok = strtok(tmp, "|");
            if (!tok) continue;
            int pat_id = atoi(tok);

            // Look up which ward this patient is in
            pthread_mutex_lock(&g_bedmap->lock);
            char found_ward[16] = {0};
            for (int i = 0; i < g_bedmap->partition_count; i++) {
                if (g_bedmap->partitions[i].patient_id == pat_id) {
                    strncpy(found_ward, g_bedmap->partitions[i].bed_type,
                            sizeof(found_ward) - 1);
                    break;
                }
            }

            if (found_ward[0] == '\0') {
                // Already discharged by another nurse — skip
                pthread_mutex_unlock(&g_bedmap->lock);
                continue;
            }

            snprintf(logbuf, sizeof(logbuf),
                     "[NURSE-%s] Discharging patient %d from %s",
                     na->ward_name, pat_id, found_ward);
            write_log(logbuf);

            free_bed(g_bedmap, pat_id);
            pthread_mutex_unlock(&g_bedmap->lock);

            // Post the semaphore for the actual ward, not this nurse's label
            sem_t *rel_sem = ward_sem(g_bedmap, strcmp(found_ward,"ICU")==0 ? 1
                                               : strcmp(found_ward,"Isolation")==0 ? 3 : 5);
            sem_post(rel_sem);

            snprintf(logbuf, sizeof(logbuf),
                     "[SEMAPHORE] %s slot freed — patient %d discharged",
                     found_ward, pat_id);
            write_log(logbuf);

            // Update metrics
            pthread_mutex_lock(&g_metrics_lock);
            for (int i = 0; i < g_metrics_count; i++) {
                if (g_metrics[i].patient_id == pat_id) {
                    g_metrics[i].complete = time(NULL);
                    break;
                }
            }
            pthread_mutex_unlock(&g_metrics_lock);
        }
        fclose(fp);
    }
    return NULL;
}
// Subpart: Nurse Threads ends
// Module 2 ends
// Subpart: Scheduling starts
/* ────────────────────────────────────────────────────────────
 * Scheduling simulation — FCFS and SJF on collected metrics
 * ──────────────────────────────────────────────────────────── */
static int cmp_arrival(const void *a, const void *b) {
    const PatientMetrics *pa = (const PatientMetrics *)a;
    const PatientMetrics *pb = (const PatientMetrics *)b;
    return (int)(pa->arrival - pb->arrival);
}

static int cmp_burst(const void *a, const void *b) {
    const PatientMetrics *pa = (const PatientMetrics *)a;
    const PatientMetrics *pb = (const PatientMetrics *)b;
    // Burst time = care_units (proxy)
    return pa->care_units - pb->care_units;
}

static int cmp_priority(const void *a, const void *b) {
    const PatientMetrics *pa = (const PatientMetrics *)a;
    const PatientMetrics *pb = (const PatientMetrics *)b;
    return pa->priority - pb->priority;
}

static void dump_scheduling_simulation(FILE *gf, PatientMetrics *arr, int n, const char *algo) {
    fprintf(gf, "\n--- %s Scheduling Simulation ---\n", algo);
    fprintf(gf, "%-8s %-16s %-10s %-10s %-12s %-12s %-12s\n",
            "PID", "Name", "Priority", "Burst", "Arrival", "Admitted", "Discharged");

    double total_wait = 0, total_turn = 0;
    int completed = 0;
    time_t clock = 0;

    for (int i = 0; i < n; i++) {
        if (arr[i].complete <= 0) continue;

        if (clock == 0) clock = arr[i].arrival;
        time_t start = (clock > arr[i].arrival) ? clock : arr[i].arrival;
        double burst = difftime(arr[i].complete, arr[i].admit);
        if (burst < 1) burst = arr[i].care_units;
        time_t end = start + (time_t)burst;

        double wait = difftime(start, arr[i].arrival);
        double turn = difftime(end, arr[i].arrival);
        total_wait += wait;
        total_turn += turn;
        completed++;

        fprintf(gf, "%-8d %-16s %-10d %-10d %-12ld %-12ld %-12ld\n",
                arr[i].patient_id, arr[i].name, arr[i].priority,
                arr[i].care_units, arr[i].arrival, start, end);
        clock = end;
    }

    if (completed > 0) {
        fprintf(gf, "\n[%s] Average Waiting Time:    %.2fs\n", algo, total_wait / completed);
        fprintf(gf, "[%s] Average Turnaround Time: %.2fs\n", algo, total_turn / completed);
    }
}

static void write_gantt_log(void) {
    FILE *gf = fopen(GANTT_LOG, "w");
    if (!gf) return;

    fprintf(gf, "=== GANTT CHART & SCHEDULING METRICS ===\n");
    fprintf(gf, "Strategy: %s\n",
            g_alloc_strategy == BEST_FIT  ? "BEST_FIT" :
            g_alloc_strategy == FIRST_FIT ? "FIRST_FIT" : "WORST_FIT");

    // Actual run metrics (Priority-based, which is how admissions works)
    fprintf(gf, "\n--- Actual Run (Priority Scheduling) ---\n");
    double total_wait = 0, total_turn = 0;
    int completed = 0;
    for (int i = 0; i < g_metrics_count; i++) {
        if (g_metrics[i].complete > 0) {
            fprintf(gf, "Patient %d (%s) [Prio=%d, Units=%d]:\n",
                    g_metrics[i].patient_id, g_metrics[i].name,
                    g_metrics[i].priority, g_metrics[i].care_units);
            fprintf(gf, "  Arrival: %ld  Admitted: %ld  Discharged: %ld\n",
                    g_metrics[i].arrival, g_metrics[i].admit, g_metrics[i].complete);
            total_wait += difftime(g_metrics[i].admit, g_metrics[i].arrival);
            total_turn += difftime(g_metrics[i].complete, g_metrics[i].arrival);
            completed++;
        }
    }
    if (completed > 0) {
        fprintf(gf, "\n[ACTUAL] Average Waiting Time:    %.2fs\n", total_wait / completed);
        fprintf(gf, "[ACTUAL] Average Turnaround Time: %.2fs\n", total_turn / completed);
    }

    /* ── Simulation 1: FCFS ─────────────────────────────────── */
    PatientMetrics sim[200];
    int n = g_metrics_count;
    memcpy(sim, g_metrics, sizeof(PatientMetrics) * (size_t)n);
    qsort(sim, (size_t)n, sizeof(PatientMetrics), cmp_arrival);
    dump_scheduling_simulation(gf, sim, n, "FCFS");

    /* ── Simulation 2: SJF ──────────────────────────────────── */
    memcpy(sim, g_metrics, sizeof(PatientMetrics) * (size_t)n);
    qsort(sim, (size_t)n, sizeof(PatientMetrics), cmp_burst);
    dump_scheduling_simulation(gf, sim, n, "SJF");

    /* ── Simulation 3: Priority ─────────────────────────────── */
    memcpy(sim, g_metrics, sizeof(PatientMetrics) * (size_t)n);
    qsort(sim, (size_t)n, sizeof(PatientMetrics), cmp_priority);
    dump_scheduling_simulation(gf, sim, n, "Priority");

    fclose(gf);
}
// Subpart: Scheduling ends
// Module: Phase 2 - Process Management / IPC / Scheduling ends
// Subpart: Main Program starts
/* ────────────────────────────────────────────────────────────
 * main — parse CLI, setup, launch threads, run, shutdown
 * ──────────────────────────────────────────────────────────── */
int main(int argc, char *argv[]) {
    // Parse --strategy flag
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--strategy") == 0 && i + 1 < argc) {
            const char *s = argv[i+1];
            if (strcmp(s, "first") == 0)      g_alloc_strategy = FIRST_FIT;
            else if (strcmp(s, "worst") == 0)  g_alloc_strategy = WORST_FIT;
            else                              g_alloc_strategy = BEST_FIT;
            i++;
        }
    }

    g_log_fp = fopen(LOG_FILE, "a");

    // Pretty startup banner
    const char *strat_name = g_alloc_strategy == BEST_FIT  ? "BEST_FIT" :
                             g_alloc_strategy == FIRST_FIT ? "FIRST_FIT" : "WORST_FIT";
    printf("\n");
    printf("%s╔══════════════════════════════════════════════════╗%s\n", CLR_CYN, CLR_RESET);
    printf("%s║%s      HOSPITAL ADMISSIONS MANAGER               %s║%s\n", CLR_CYN, CLR_BOLD, CLR_CYN, CLR_RESET);
    printf("%s║%s      CL2006 OS Lab                             %s║%s\n", CLR_CYN, CLR_RESET, CLR_CYN, CLR_RESET);
    printf("%s╠══════════════════════════════════════════════════╣%s\n", CLR_CYN, CLR_RESET);
    printf("%s║%s  Strategy : %s%-37s%s║%s\n", CLR_CYN, CLR_RESET, CLR_GRN, strat_name, CLR_CYN, CLR_RESET);
    printf("%s║%s  PID      : %s%-37d%s║%s\n", CLR_CYN, CLR_RESET, CLR_GRN, getpid(), CLR_CYN, CLR_RESET);
    printf("%s║%s  Wards    : ICU(%d)  Isolation(%d)  General(%d)    %s║%s\n", CLR_CYN, CLR_RESET, ICU_PARTITIONS, ISOLATION_PARTITIONS, GENERAL_PARTITIONS, CLR_CYN, CLR_RESET);
    printf("%s╠══════════════════════════════════════════════════╣%s\n", CLR_CYN, CLR_RESET);
    printf("%s║%s  Threads  : Receptionist, Scheduler, 3× Nurse  %s║%s\n", CLR_CYN, CLR_RESET, CLR_CYN, CLR_RESET);
    printf("%s║%s  IPC      : FIFO (triage + sim), POSIX shm     %s║%s\n", CLR_CYN, CLR_RESET, CLR_CYN, CLR_RESET);
    printf("%s║%s  Bonus    : SIGUSR1 cycles allocation strategy  %s║%s\n", CLR_CYN, CLR_RESET, CLR_CYN, CLR_RESET);
    printf("%s╚══════════════════════════════════════════════════╝%s\n\n", CLR_CYN, CLR_RESET);
    fflush(stdout);

    {
        char msg[128];
        snprintf(msg, sizeof(msg), "=== Admissions Manager Starting (strategy=%s) ===", strat_name);
        write_log(msg);
    }

    FILE *pf = fopen(PID_FILE, "w");
    if (pf) { fprintf(pf, "%d\n", getpid()); fclose(pf); }

    // Signal handlers
    struct sigaction sa_chld = { .sa_handler = sigchld_handler, .sa_flags = SA_RESTART | SA_NOCLDSTOP };
    sigemptyset(&sa_chld.sa_mask);
    sigaction(SIGCHLD, &sa_chld, NULL);

    struct sigaction sa_term = { .sa_handler = sigterm_handler, .sa_flags = 0 };
    sigemptyset(&sa_term.sa_mask);
    sigaction(SIGTERM, &sa_term, NULL);
    sigaction(SIGINT,  &sa_term, NULL);

    // Bonus: SIGUSR1 cycles allocation strategy at runtime
    struct sigaction sa_usr1 = { .sa_handler = sigusr1_handler, .sa_flags = 0 };
    sigemptyset(&sa_usr1.sa_mask);
    sigaction(SIGUSR1, &sa_usr1, NULL);

    mkfifo(TRIAGE_FIFO, 0660);
    mkfifo(SIM_FIFO,    0660);

    g_bedmap = shm_create();
    if (!g_bedmap) return EXIT_FAILURE;

    queue_init(&g_queue);

    /* ── Launch threads ───────────────────────────────────── */
    pthread_t rec_tid, sch_tid;
    pthread_t nurse_icu_tid, nurse_iso_tid, nurse_gen_tid;

    // 3 Nurse threads — one per bed type
    static NurseArg na_icu = { "ICU" };
    static NurseArg na_iso = { "Isolation" };
    static NurseArg na_gen = { "General" };

    pthread_create(&rec_tid,       NULL, receptionist_thread, NULL);
    pthread_create(&sch_tid,       NULL, scheduler_thread,    NULL);
    pthread_create(&nurse_icu_tid, NULL, nurse_thread,        &na_icu);
    pthread_create(&nurse_iso_tid, NULL, nurse_thread,        &na_iso);
    pthread_create(&nurse_gen_tid, NULL, nurse_thread,        &na_gen);

    // Wait for shutdown signal
    while (g_running) { pause(); }

    /* ── Graceful thread shutdown ─────────────────────────── */
    pthread_cancel(rec_tid);
    pthread_cancel(sch_tid);
    pthread_cancel(nurse_icu_tid);
    pthread_cancel(nurse_iso_tid);
    pthread_cancel(nurse_gen_tid);

    // Unblock any thread waiting on FIFO opens
    int tmp;
    tmp = open(TRIAGE_FIFO, O_WRONLY | O_NONBLOCK); if (tmp >= 0) close(tmp);
    tmp = open(SIM_FIFO,    O_WRONLY | O_NONBLOCK); if (tmp >= 0) close(tmp);

    // Unblock queue semaphores
    sem_post(&g_queue.slots_used);
    sem_post(&g_queue.slots_free);
    pthread_cond_broadcast(&g_queue.ready);

    pthread_join(rec_tid, NULL);
    pthread_join(sch_tid, NULL);
    pthread_join(nurse_icu_tid, NULL);
    pthread_join(nurse_iso_tid, NULL);
    pthread_join(nurse_gen_tid, NULL);

    /* ── Dump Gantt chart & scheduling simulation ─────────── */
    write_gantt_log();

    shm_destroy(g_bedmap);
    remove(PID_FILE);
    write_log("=== Admissions Manager Exited Cleanly ===");

    // Pretty shutdown summary
    printf("\n%s╔══════════════════════════════════════════════════╗%s\n", CLR_CYN, CLR_RESET);
    printf("%s║%s      SHUTDOWN COMPLETE                         %s║%s\n", CLR_CYN, CLR_GRN, CLR_CYN, CLR_RESET);
    printf("%s╠══════════════════════════════════════════════════==╣%s\n", CLR_CYN, CLR_RESET);
    printf("%s║%s  Patients processed : %s%-27d%s║%s\n", CLR_CYN, CLR_RESET, CLR_GRN, g_metrics_count, CLR_CYN, CLR_RESET);
    {
        int completed = 0;
        for (int i = 0; i < g_metrics_count; i++)
            if (g_metrics[i].complete > 0) completed++;
        printf("%s║%s  Discharged         : %s%-27d%s║%s\n", CLR_CYN, CLR_RESET, CLR_GRN, completed, CLR_CYN, CLR_RESET);
    }
    printf("%s║%s  Schedule log       : %sschedule_log.txt%s            %s║%s\n", CLR_CYN, CLR_RESET, CLR_YLW, CLR_RESET, CLR_CYN, CLR_RESET);
    printf("%s║%s  Memory log         : %smemory_log.txt%s              %s║%s\n", CLR_CYN, CLR_RESET, CLR_YLW, CLR_RESET, CLR_CYN, CLR_RESET);
    printf("%s║%s  Patient records    : %spatient_records.dat%s         %s║%s\n", CLR_CYN, CLR_RESET, CLR_YLW, CLR_RESET, CLR_CYN, CLR_RESET);
    printf("%s╚══════════════════════════════════════════════════╝%s\n\n", CLR_CYN, CLR_RESET);
    fflush(stdout);

    if (g_log_fp) fclose(g_log_fp);

    return EXIT_SUCCESS;
}
// Subpart: Main Program ends