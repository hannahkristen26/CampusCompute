/*
 * main.c - FR8: command-line interface, error handling and the integrated demo
 *
 * Exit codes: 0 = success, 1 = runtime failure, 2 = usage / input error.
 */
#include "campuscompute.h"

static void usage(FILE *out)
{
    fprintf(out,
        "CampusCompute - Intelligent Job Scheduling and Resource Management\n"
        "Usage: campuscompute <command> [options]\n\n"
        "Commands:\n"
        "  sched fcfs|rr|priority|compare [-f JOBFILE] [-q QUANTUM]\n"
        "                          CPU scheduling (FR1, FR2); default job set is built in\n"
        "  logger [P [E [CAP]]]    producer-consumer logger with pthreads (FR3)\n"
        "  ipc [MESSAGES]          shared memory + named semaphore IPC (FR4)\n"
        "  banker safe|unsafe|FILE Banker's safety algorithm (FR5)\n"
        "  workers normal|fail     fork()/waitpid() workers, simulated failures (FR6)\n"
        "  limits [files|mem|cpu|all]\n"
        "                          setrlimit() demonstrations (FR7)\n"
        "  verify                  check that no IPC objects/children/threads are left (FR7)\n"
        "  all                     run every feature in turn and print a summary (integrated demo)\n"
        "  help                    show this message\n\n"
        "Examples:\n"
        "  ./campuscompute sched rr -q 3\n"
        "  ./campuscompute sched compare -f samples/jobs.txt\n"
        "  ./campuscompute banker samples/banker_unsafe.txt\n");
}

typedef struct {
    const char *name;
    int passed;
} StepResult;

/* Integrated demonstration: run every feature and summarise. */
static int run_all(void)
{
    StepResult res[12];
    int n = 0, failures = 0, r;
    char *sched_args[]   = { "compare" };
    char *logger_args[]  = { "3", "6", "4" };
    char *ipc_args[]     = { "5" };
    char *workers_n[]    = { "normal" };
    char *workers_f[]    = { "fail" };
    char *limits_args[]  = { "all" };

    print_banner("[1/9] CPU scheduling: FCFS vs Round Robin vs Priority");
    r = cmd_sched(1, sched_args);
    res[n].name = "CPU scheduling comparison";      res[n++].passed = (r == 0);

    print_banner("[2/9] Concurrent logger (producer-consumer, pthreads)");
    r = cmd_logger(3, logger_args);
    res[n].name = "Producer-consumer logger";       res[n++].passed = (r == 0);

    print_banner("[3/9] Shared-memory IPC with a named semaphore");
    r = cmd_ipc(1, ipc_args);
    res[n].name = "Shared memory + semaphore IPC";  res[n++].passed = (r == 0);

    print_banner("[4/9] Banker's algorithm - SAFE state");
    r = banker_run_case(1);
    res[n].name = "Banker's safe state detected";   res[n++].passed = (r == 1);

    print_banner("[5/9] Banker's algorithm - UNSAFE state");
    r = banker_run_case(0);
    res[n].name = "Banker's unsafe state detected"; res[n++].passed = (r == 0);

    print_banner("[6/9] Worker processes - normal completion");
    r = cmd_workers(1, workers_n);
    res[n].name = "Workers: normal completion";     res[n++].passed = (r == 0);

    print_banner("[7/9] Worker processes - simulated failures");
    r = cmd_workers(1, workers_f);
    res[n].name = "Workers: failure detection";     res[n++].passed = (r == 0);

    print_banner("[8/9] Resource limits (setrlimit)");
    r = cmd_limits(1, limits_args);
    res[n].name = "Resource limits";                res[n++].passed = (r == 0);

    print_banner("[9/9] Cleanup verification");
    r = cmd_verify(0, NULL);
    res[n].name = "Cleanup verification";           res[n++].passed = (r == 0);

    print_banner("INTEGRATED DEMONSTRATION SUMMARY");
    for (int i = 0; i < n; i++) {
        printf("  %-34s : %s\n", res[i].name, res[i].passed ? "PASS" : "FAIL");
        if (!res[i].passed)
            failures++;
    }
    printf("\n  %d of %d checks passed. Log file written: campuscompute.log\n", n - failures, n);
    return failures == 0 ? 0 : 1;
}

int main(int argc, char **argv)
{
    const char *cmd;
    int sub_argc;
    char **sub_argv;

    /* Line-buffered stdout keeps output ordered even when several processes
     * and threads print, and when the output is piped through tee/less. */
    setvbuf(stdout, NULL, _IOLBF, 0);

    if (argc < 2) {
        fprintf(stderr, "Error: no command given.\n\n");
        usage(stderr);
        return 2;
    }

    cmd = argv[1];
    sub_argc = argc - 2;
    sub_argv = argv + 2;

    if (strcmp(cmd, "help") == 0 || strcmp(cmd, "-h") == 0 || strcmp(cmd, "--help") == 0) {
        usage(stdout);
        return 0;
    }
    if (strcmp(cmd, "sched") == 0)   return cmd_sched(sub_argc, sub_argv);
    if (strcmp(cmd, "logger") == 0)  return cmd_logger(sub_argc, sub_argv);
    if (strcmp(cmd, "ipc") == 0)     return cmd_ipc(sub_argc, sub_argv);
    if (strcmp(cmd, "banker") == 0)  return cmd_banker(sub_argc, sub_argv);
    if (strcmp(cmd, "workers") == 0) return cmd_workers(sub_argc, sub_argv);
    if (strcmp(cmd, "limits") == 0)  return cmd_limits(sub_argc, sub_argv);
    if (strcmp(cmd, "verify") == 0)  return cmd_verify(sub_argc, sub_argv);
    if (strcmp(cmd, "all") == 0) {
        if (sub_argc != 0) {
            fprintf(stderr, "Error: 'all' takes no arguments.\n");
            return 2;
        }
        return run_all();
    }

    fprintf(stderr, "Error: unknown command '%s'.\nRun './campuscompute help' to see the available commands.\n", cmd);
    return 2;
}
