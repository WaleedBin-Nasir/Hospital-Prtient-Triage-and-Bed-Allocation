/******************************************************************************
 * Project: Hospital Patient Triage & Bed Allocator
 * File: bed.h
 * Group: Group XX
 * Members: Muhammad Abdullah khan, Muhammad Zunair Haider, Waleed bin Nasir
 * Roll No.: 24F-0626,24F-590,24F-0516
 * Date: 2026-05-08
 ******************************************************************************/
#ifndef BED_H
#define BED_H
#include <stdint.h>
#include <pthread.h>
#include <semaphore.h>
// Ward capacities in terms of partitions and units 
#define ICU_PARTITIONS       4
#define ICU_UNITS_PER        3
#define ISOLATION_PARTITIONS 4
#define ISOLATION_UNITS_PER  2
#define GENERAL_PARTITIONS   12
#define GENERAL_UNITS_PER    1
#define TOTAL_UNITS (ICU_PARTITIONS*ICU_UNITS_PER + ISOLATION_PARTITIONS*ISOLATION_UNITS_PER + GENERAL_PARTITIONS*GENERAL_UNITS_PER)
// Ward start indices in the unit array 
#define ICU_START       0
#define ISOLATION_START (ICU_START + ICU_PARTITIONS * ICU_UNITS_PER)
#define GENERAL_START   (ISOLATION_START + ISOLATION_PARTITIONS * ISOLATION_UNITS_PER)
//Paging simulation — page size in care units 
#define PAGE_SIZE 2
#define TOTAL_PAGES ((TOTAL_UNITS + PAGE_SIZE - 1) / PAGE_SIZE)
typedef enum 
{
    FIRST_FIT = 0,
    BEST_FIT  = 1,
    WORST_FIT = 2
} AllocStrategy;
// Single bed partition in the ward memory model 
typedef struct 
{
    int partition_id;
    int start_unit;   //index in ward array 
    int size;         // number of care units 
    int is_free;      // 1 = FREE , 0 = OCCUPIED 
    int patient_id;   // -1 if free 
    char bed_type[16];// "ICU", "GENERAL", "ISOLATION" 
} BedPartition;
/*
 * Shared memory layout — lives in POSIX shm object.
 * We track max MAX_PARTITIONS (which could be up to TOTAL_UNITS if fragmented)
 */
#define MAX_PARTITIONS 64
typedef struct 
{
    BedPartition partitions[MAX_PARTITIONS];
    int          partition_count;
    /* Page table: maps each page to a patient_id (-1 = free) */
    int          page_table[TOTAL_PAGES];
    pthread_mutex_t lock;
    pthread_cond_t  bed_free;     // broadcast when a bed is released 
    //Counting semaphores for each ward 
    sem_t        icu_sem;
    sem_t        iso_sem;
    sem_t        gen_sem;
} BedMap;
#define SHM_NAME  "/hospital_beds"
#endif
