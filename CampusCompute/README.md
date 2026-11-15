# CampusCompute

Intelligent Job Scheduling and Resource Management
OPERATING SYSTEMS (UE25CS242A) - Level 3 Mini-Project

A command-line prototype, written in C for Linux, that demonstrates CPU
scheduling, a multi-threaded logger, POSIX shared-memory IPC, deadlock
avoidance (Banker's algorithm), worker-failure detection and process resource
limits. All output is produced by running code; nothing is hard-coded.

## Prerequisites

* Ubuntu Linux or WSL Ubuntu (glibc, POSIX threads, `/dev/shm`)
* `gcc` and `make`  ->  `sudo apt install build-essential`
* No root access and no third-party libraries are needed.

## Build

```
make            # builds ./campuscompute
make clean      # removes object files, the executable and campuscompute.log
```

Exact build command (what `make` runs):

```
gcc -std=gnu11 -Wall -Wextra -O2 -pthread -o campuscompute main.c util.c scheduler.c \
    logger.c ipc.c banker.c workers.c limits.c -pthread -lrt
```

## Run

```
./campuscompute help                       # list all commands
./campuscompute all                        # integrated demonstration of every feature
make run                                   # same as ./campuscompute all
```

| Command | Feature | Requirement |
|---|---|---|
| `sched fcfs` / `sched rr [-q N]` / `sched priority` | CPU scheduling with Gantt chart, completion/turnaround/waiting times and averages | FR1, FR2 |
| `sched compare [-f FILE] [-q N]` | all three policies on the same job set, side by side | FR2 |
| `logger [P [E [CAP]]]` | P producer threads, 1 consumer, queue of CAP slots, writes `campuscompute.log` | FR3 |
| `ipc [MESSAGES]` | parent/child exchange through POSIX shared memory + named semaphores | FR4 |
| `banker safe` / `banker unsafe` / `banker FILE` | Banker's safety algorithm with step-by-step trace | FR5 |
| `workers normal` / `workers fail` | `fork()` + `waitpid()`; `fail` injects two SIMULATED failures | FR6 |
| `limits [files\|mem\|cpu\|all]` | `setrlimit()` demonstrations, each in its own child | FR7 |
| `verify` | confirms IPC objects, child processes and threads are gone | FR7 |
| invalid command / bad input | usage message and exit code 2 | FR8 |

Exit codes: `0` success, `1` runtime failure, `2` usage or input error.

### Job file format (`-f FILE`)

One job per line: `id arrival burst priority`. `#` starts a comment.
Ranges: id 1..9999 (unique), arrival 0..1000, burst 1..100, priority 1..10, at most 32 jobs.
Without `-f`, a built-in sample set (identical to `samples/jobs.txt`) is used.

### Banker's file format

```
n m                      # processes, resource types
Available                # m numbers
Maximum                  # n rows of m numbers
Allocation               # n rows of m numbers
```

## Scheduling rules

* **FCFS**: earlier arrival first; equal arrival -> lower job ID first.
* **Priority (non-preemptive)**: lower number = higher priority; equal priority -> earlier arrival; still equal -> lower job ID.
* **Round Robin**: default time quantum **2** (change with `-q N`). FIFO ready queue; a job that arrives exactly when a quantum expires is queued *before* the preempted job.
* Completion time = time the job finishes; turnaround = completion - arrival; waiting = turnaround - burst.
* "Switches" in the comparison table = direct job-to-job CPU hand-overs in the Gantt chart.

## Design notes

* **Logger (FR3):** one mutex guards the circular queue; condition variable `not_full` blocks producers when the queue is full and `not_empty` blocks the consumer when it is empty. At shutdown the main thread joins all producers, sets a "finished" flag under the mutex, broadcasts `not_empty`, and joins the consumer after it has drained the queue. The run verifies produced = consumed = lines written.
* **IPC (FR4):** a single-slot mailbox in shared memory. Named semaphores `empty` (1) and `full` (0) alternate the writer and reader; waits use a 5-second timeout so a dead peer cannot hang the program. The child opens the objects by name.
* **Failures (FR6):** worker failures are **simulated on purpose** (exit status 3 and a self-sent SIGTERM) and every line about them says so. They are not real operating-system faults. The manager detects them from the `waitpid()` status.
* **Limits (FR7):** `RLIMIT_NOFILE`, `RLIMIT_AS` and `RLIMIT_CPU` are applied in child processes only; the manager's own limits are unchanged.

## Resources created and cleanup

The program creates only:

| Resource | Name | Removed by |
|---|---|---|
| shared memory | `/campuscompute_shm_<uid>` | `ipc` itself (`shm_unlink`) |
| named semaphores | `/campuscompute_empty_<uid>`, `/campuscompute_full_<uid>` | `ipc` itself (`sem_unlink`) |
| log file | `campuscompute.log` (current directory) | `make clean` |

Threads are joined and child processes are reaped before each command returns.

Cleanup / verification commands:

```
./campuscompute verify                      # reports released / still present
ls /dev/shm | grep campuscompute            # should print nothing
pgrep -a campuscompute                      # should print nothing
make clean                                  # remove build products and the log file
```

If a run is killed with Ctrl+C or `kill -9`, the next `./campuscompute ipc` removes its own stale
objects automatically. To remove them by hand (only these three names are ever created):

```
rm -f /dev/shm/campuscompute_shm_$(id -u) /dev/shm/sem.campuscompute_empty_$(id -u) /dev/shm/sem.campuscompute_full_$(id -u)
```

## Files

```
Makefile  README.md
campuscompute.h                 shared structures and prototypes
main.c                          CLI, error handling, integrated demo
util.c                          number parsing, helpers
scheduler.c                     FR1 + FR2
logger.c                        FR3
ipc.c                           FR4 and cleanup verification
banker.c                        FR5
workers.c                       FR6
limits.c                        FR7
samples/jobs.txt                sample job set
samples/jobs_invalid.txt        malformed input for error-handling tests
samples/banker_safe.txt         safe Banker's state
samples/banker_unsafe.txt       unsafe Banker's state
```
