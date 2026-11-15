/*
 * workers.c - FR6: worker failure detection with fork() and waitpid()
 *
 * The manager forks four workers. In "fail" mode two of them are told to
 * fail ON PURPOSE:
 *     worker 3 : exits with status 3                (SIMULATED failure)
 *     worker 4 : sends itself SIGTERM               (SIMULATED failure)
 * These are deliberate, controlled simulations - not real OS faults - and
 * every line that concerns them says so. The manager collects every child
 * with waitpid() and decodes the status with WIFEXITED/WEXITSTATUS and
 * WIFSIGNALED/WTERMSIG, so a failure is detected from the status alone.
 */
#include "campuscompute.h"
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

#define NUM_WORKERS 4

typedef enum { W_NORMAL, W_SIM_EXIT, W_SIM_SIGNAL } WorkerMode;

static const char *mode_text(WorkerMode m)
{
    switch (m) {
    case W_NORMAL:     return "will complete normally";
    case W_SIM_EXIT:   return "SIMULATED failure: will exit with status 3";
    case W_SIM_SIGNAL: return "SIMULATED failure: will terminate itself with SIGTERM";
    }
    return "?";
}

/* Child process body; never returns. */
static void worker_child(int id, WorkerMode mode)
{
    volatile unsigned long sum = 0;

    printf("[WORKER %d pid=%d] started\n", id, (int)getpid());
    fflush(stdout);

    sleep_ms(150L * id);                         /* stagger so the completions are visible */
    for (long i = 1; i <= 3000000L; i++)         /* a little CPU work */
        sum += (unsigned long)((i * id) % 7);

    if (mode == W_SIM_EXIT) {
        printf("[WORKER %d pid=%d] *** SIMULATED FAILURE (deliberate): exiting with status 3 ***\n",
               id, (int)getpid());
        fflush(stdout);
        _exit(3);
    }
    if (mode == W_SIM_SIGNAL) {
        printf("[WORKER %d pid=%d] *** SIMULATED FAILURE (deliberate): raising SIGTERM on itself ***\n",
               id, (int)getpid());
        fflush(stdout);
        raise(SIGTERM);
        _exit(99);                               /* not reached if SIGTERM terminates us */
    }

    printf("[WORKER %d pid=%d] finished normally (checksum %lu)\n", id, (int)getpid(), (unsigned long)sum);
    fflush(stdout);
    _exit(0);
}

static int workers_usage(void)
{
    fprintf(stderr, "Usage: campuscompute workers <normal|fail>\n"
                    "  normal : 4 workers, all complete normally\n"
                    "  fail   : 4 workers, workers 3 and 4 fail on purpose (simulated)\n");
    return 2;
}

int cmd_workers(int argc, char **argv)
{
    WorkerMode mode[NUM_WORKERS] = { W_NORMAL, W_NORMAL, W_NORMAL, W_NORMAL };
    pid_t pid[NUM_WORKERS] = { 0 };
    int spawned = 0, completed = 0, failed = 0, simulated_failed = 0, unexpected = 0;
    int expected_failures = 0;

    if (argc != 1)
        return workers_usage();
    if (strcmp(argv[0], "fail") == 0) {
        mode[2] = W_SIM_EXIT;
        mode[3] = W_SIM_SIGNAL;
        expected_failures = 2;
    } else if (strcmp(argv[0], "normal") != 0) {
        fprintf(stderr, "Error: unknown workers mode '%s'\n", argv[0]);
        return workers_usage();
    }

    printf("[MANAGER pid=%d] starting %d workers (mode: %s)\n", (int)getpid(), NUM_WORKERS, argv[0]);
    for (int i = 0; i < NUM_WORKERS; i++) {
        fflush(stdout);
        pid[i] = fork();
        if (pid[i] < 0) {
            perror("fork");
            break;
        }
        if (pid[i] == 0)
            worker_child(i + 1, mode[i]);
        spawned++;
        printf("[MANAGER] spawned worker %d as pid %d (%s)\n", i + 1, (int)pid[i], mode_text(mode[i]));
    }

    for (int k = 0; k < spawned; k++) {
        int status, idx = -1;
        pid_t done = waitpid(-1, &status, 0);       /* completion order, not spawn order */

        if (done < 0) {
            if (errno == EINTR) { k--; continue; }
            perror("waitpid");
            break;
        }
        for (int i = 0; i < spawned; i++)
            if (pid[i] == done)
                idx = i;

        if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
            completed++;
            printf("[MANAGER] worker %d (pid %d): COMPLETED normally, exit status 0\n", idx + 1, (int)done);
            if (mode[idx] != W_NORMAL)
                unexpected++;
        } else {
            failed++;
            if (WIFEXITED(status))
                printf("[MANAGER] worker %d (pid %d): FAILURE DETECTED - exited with status %d%s\n",
                       idx + 1, (int)done, WEXITSTATUS(status),
                       mode[idx] != W_NORMAL ? "  [simulated by this program]" : "  [UNEXPECTED real failure]");
            else if (WIFSIGNALED(status))
                printf("[MANAGER] worker %d (pid %d): FAILURE DETECTED - killed by signal %d (%s)%s\n",
                       idx + 1, (int)done, WTERMSIG(status), strsignal(WTERMSIG(status)),
                       mode[idx] != W_NORMAL ? "  [simulated by this program]" : "  [UNEXPECTED real failure]");
            if (mode[idx] != W_NORMAL)
                simulated_failed++;
            else
                unexpected++;
        }
    }

    /* Every child must have been reaped: waitpid must now report "no children". */
    if (waitpid(-1, NULL, WNOHANG) == -1 && errno == ECHILD)
        printf("[MANAGER] all child processes reaped - no zombies left\n");
    else
        printf("[MANAGER] WARNING: a child process was not reaped\n");

    printf("\nWorker summary: %d started, %d completed normally, %d failed (%d simulated, %d unexpected)\n",
           spawned, completed, failed, simulated_failed, unexpected);

    if (spawned == NUM_WORKERS && failed == expected_failures && unexpected == 0) {
        printf("RESULT: PASS - outcome matches the plan (%d failure%s %s)\n", expected_failures,
               expected_failures == 1 ? "" : "s", expected_failures ? "detected and reported" : "expected");
        return 0;
    }
    printf("RESULT: FAIL\n");
    return 1;
}
