/******************************************************************************
 * Project: Hospital Patient Triage & Bed Allocator
 * File: ipc.h
 * Group: Group XX
 * Members: Muhammad Abdullah khan, Muhammad Zunair Haider, Waleed bin Nasir
 * Roll No.: 24F-0626,24F-590,24F-0516
 * Date: 2026-05-08
 ******************************************************************************/
#ifndef IPC_H
#define IPC_H
// Named FIFO paths 
#define TRIAGE_FIFO  "/tmp/hospital_triage.fifo"   // triage.sh → admissions 
#define SIM_FIFO     "/tmp/discharge_fifo"          // patient_simulator → admissions 
//PID file for stop script 
#define PID_FILE     "/tmp/hospital_admissions.pid"
// Log file 
#define LOG_FILE     "logs/admissions.log"
/*
 * Wire format over FIFOs — pipe-delimited string:
 *   name|age|severity|symptoms\n
 * Example:
 *   Alice Smith|34|0|chest pain,shortness of breath\n
 *
 * Simulator result format:
 *   pid|bed_index|status\n
 *   status: "OK" or "DISCHARGE"
 */
#define RECORD_DELIM  '|'
#define MAX_LINE      512
#endif 
