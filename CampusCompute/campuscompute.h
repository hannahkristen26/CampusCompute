/*
 * campuscompute.h - shared declarations for CampusCompute
 * OPERATING SYSTEMS (UE25CS242A) - Level 3 Mini-Project
 *
 * Every feature module exposes a cmd_xxx(argc, argv) entry point that
 * receives the arguments AFTER the command name, so each feature can be
 * tested on its own from the command line.
 *
 * Return-value convention for cmd_xxx(): 0 = success, 1 = runtime failure,
 *                                        2 = usage / input error.
 */
#ifndef CAMPUSCOMPUTE_H
#define CAMPUSCOMPUTE_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

/* ------------------------------------------------------------------ */
/* FR1: job model                                                      */
/* ------------------------------------------------------------------ */
#define MAX_JOBS        32      /* maximum jobs accepted from input        */
#define MAX_SEGMENTS    4096    /* > MAX_JOBS * max burst (100) + idle gaps */
#define DEFAULT_QUANTUM 2       /* default Round Robin time quantum         */

typedef enum { JOB_NEW, JOB_READY, JOB_RUNNING, JOB_COMPLETED } JobStatus;

typedef struct {
    int id;             /* unique job ID (1..9999)                 */
    int arrival;        /* arrival time (0..1000)                  */
    int burst;          /* CPU burst (1..100)                      */
    int priority;       /* 1..10, LOWER number = HIGHER priority   */
    JobStatus status;   /* lifecycle state                         */
    int remaining;      /* CPU time still needed                   */
    int start_time;     /* first time the job got the CPU (-1 = never) */
    int completion;     /* completion time                         */
    int turnaround;     /* completion - arrival                    */
    int waiting;        /* turnaround - burst                      */
} Job;

/* One slice of the Gantt chart; job_id == -1 means the CPU was idle. */
typedef struct { int job_id; int start; int end; } Segment;
typedef struct { Segment seg[MAX_SEGMENTS]; int count; } Timeline;

typedef struct {
    double avg_completion;
    double avg_turnaround;
    double avg_waiting;
    double cpu_util;        /* percent of time the CPU was busy */
    int    total_time;      /* time at which the last job finished */
    int    switches;        /* direct job-to-job CPU hand-overs     */
} SchedStats;

/* ------------------------------------------------------------------ */
/* util.c                                                              */
/* ------------------------------------------------------------------ */
int    parse_int(const char *text, long min, long max, int *out);
int    parse_int_line(const char *line, long *vals, int want);
int    load_int_tokens(const char *path, long *vals, int max_vals, int *count);
void   sleep_ms(long ms);
double monotonic_seconds(void);
void   print_banner(const char *title);

/* ------------------------------------------------------------------ */
/* feature modules                                                     */
/* ------------------------------------------------------------------ */
int cmd_sched(int argc, char **argv);            /* scheduler.c  FR1, FR2 */
int cmd_logger(int argc, char **argv);           /* logger.c     FR3      */
int cmd_ipc(int argc, char **argv);              /* ipc.c        FR4      */
int cmd_verify(int argc, char **argv);           /* ipc.c        FR7/S20  */
int cmd_banker(int argc, char **argv);           /* banker.c     FR5      */
int banker_run_case(int safe_case);              /* 1 safe, 0 unsafe, -1 error */
int cmd_workers(int argc, char **argv);          /* workers.c    FR6      */
int cmd_limits(int argc, char **argv);           /* limits.c     FR7      */

#endif /* CAMPUSCOMPUTE_H */
