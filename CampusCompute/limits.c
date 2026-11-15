/*
 * limits.c - FR7: process resource limits with setrlimit()
 *
 * Each test runs in its OWN child process, so the limit is applied only to
 * that child and the manager (parent) is not affected.
 *
 *   files : RLIMIT_NOFILE  open() starts failing with EMFILE
 *   mem   : RLIMIT_AS      malloc() starts returning NULL (ENOMEM)
 *   cpu   : RLIMIT_CPU     the kernel kills the process with SIGXCPU
 */
#include "campuscompute.h"
#include <fcntl.h>
#include <signal.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>

#define NOFILE_LIMIT   16
#define AS_LIMIT_BYTES (128UL * 1024UL * 1024UL)
#define CHUNK_BYTES    (8UL * 1024UL * 1024UL)
#define MAX_CHUNKS     64

/* ---- child bodies (never return) ---------------------------------- */

static void child_files(void)
{
    struct rlimit rl = { NOFILE_LIMIT, NOFILE_LIMIT };
    int fds[MAX_CHUNKS], opened = 0;

    if (setrlimit(RLIMIT_NOFILE, &rl) != 0) { perror("[CHILD] setrlimit(RLIMIT_NOFILE)"); _exit(2); }
    getrlimit(RLIMIT_NOFILE, &rl);
    printf("[CHILD pid=%d] RLIMIT_NOFILE is now soft=%lu hard=%lu\n", (int)getpid(),
           (unsigned long)rl.rlim_cur, (unsigned long)rl.rlim_max);
    fflush(stdout);

    while (opened < MAX_CHUNKS) {
        int fd = open("/dev/null", O_RDONLY);
        if (fd < 0) {
            printf("[CHILD] open() #%d FAILED: %s (errno=%d)\n", opened + 1, strerror(errno), errno);
            break;
        }
        fds[opened++] = fd;
    }
    printf("[CHILD] opened %d extra descriptors before the limit was hit\n", opened);
    fflush(stdout);
    for (int i = 0; i < opened; i++)
        close(fds[i]);
    _exit(opened < MAX_CHUNKS ? 0 : 3);          /* 3 = the limit was NOT enforced */
}

static void child_memory(void)
{
    struct rlimit rl = { AS_LIMIT_BYTES, AS_LIMIT_BYTES };
    void *chunks[MAX_CHUNKS];
    int got = 0;

    if (setrlimit(RLIMIT_AS, &rl) != 0) { perror("[CHILD] setrlimit(RLIMIT_AS)"); _exit(2); }
    printf("[CHILD pid=%d] RLIMIT_AS set to %lu MiB; allocating %lu MiB chunks\n", (int)getpid(),
           AS_LIMIT_BYTES >> 20, CHUNK_BYTES >> 20);
    fflush(stdout);

    while (got < MAX_CHUNKS) {
        void *p = malloc(CHUNK_BYTES);
        if (p == NULL) {
            printf("[CHILD] malloc(%lu MiB) FAILED after %d chunks (%lu MiB): %s\n", CHUNK_BYTES >> 20,
                   got, (got * CHUNK_BYTES) >> 20, strerror(errno));
            break;
        }
        chunks[got++] = p;
    }
    fflush(stdout);
    for (int i = 0; i < got; i++)
        free(chunks[i]);
    _exit(got < MAX_CHUNKS ? 0 : 3);
}

static void child_cpu(void)
{
    struct rlimit core = { 0, 0 };               /* no core file for the intentional kill */
    struct rlimit rl = { 1, 2 };                 /* soft 1 s, hard 2 s of CPU time */
    volatile unsigned long spin = 0;

    setrlimit(RLIMIT_CORE, &core);
    if (setrlimit(RLIMIT_CPU, &rl) != 0) { perror("[CHILD] setrlimit(RLIMIT_CPU)"); _exit(2); }
    printf("[CHILD pid=%d] RLIMIT_CPU set to %lu s (soft) / %lu s (hard); spinning in a busy loop...\n",
           (int)getpid(), (unsigned long)rl.rlim_cur, (unsigned long)rl.rlim_max);
    fflush(stdout);

    alarm(6);                                    /* safety net if the limit is not enforced */
    for (;;)
        spin++;
}

/* ---- parent side -------------------------------------------------- */

/* Run fn in a child and decode its exit status. Returns the raw wait status. */
static int run_child(void (*fn)(void), double *elapsed)
{
    int status = 0;
    pid_t pid;
    double t0;

    fflush(stdout);
    t0 = monotonic_seconds();
    pid = fork();
    if (pid < 0) {
        perror("fork");
        return -1;
    }
    if (pid == 0)
        fn();
    while (waitpid(pid, &status, 0) < 0 && errno == EINTR)
        ;
    *elapsed = monotonic_seconds() - t0;
    return status;
}

static void show_parent_limit(int resource, const char *name)
{
    struct rlimit rl;

    if (getrlimit(resource, &rl) == 0) {
        if (rl.rlim_cur == RLIM_INFINITY)
            printf("[PARENT] its own %s is unchanged: soft=unlimited\n", name);
        else
            printf("[PARENT] its own %s is unchanged: soft=%lu\n", name, (unsigned long)rl.rlim_cur);
    }
}

static int test_files(void)
{
    double t;
    int st = run_child(child_files, &t);

    printf("[PARENT] child status: %s\n", (st != -1 && WIFEXITED(st)) ? "exited" : "abnormal");
    if (st != -1 && WIFEXITED(st) && WEXITSTATUS(st) == 0) {
        printf("[PARENT] OBSERVED EFFECT: once the descriptor table was full, open() failed with EMFILE\n"
               "         (\"Too many open files\") instead of the process getting more descriptors.\n");
        show_parent_limit(RLIMIT_NOFILE, "RLIMIT_NOFILE");
        return 0;
    }
    printf("[PARENT] FAIL: limit not observed (exit status %d)\n", st == -1 ? -1 : WEXITSTATUS(st));
    return 1;
}

static int test_memory(void)
{
    double t;
    int st = run_child(child_memory, &t);

    printf("[PARENT] child status: %s\n", (st != -1 && WIFEXITED(st)) ? "exited" : "abnormal");
    if (st != -1 && WIFEXITED(st) && WEXITSTATUS(st) == 0) {
        printf("[PARENT] OBSERVED EFFECT: allocations succeeded until the address-space cap was reached,\n"
               "         then malloc() returned NULL with ENOMEM; the process stayed alive and handled it.\n");
        show_parent_limit(RLIMIT_AS, "RLIMIT_AS");
        return 0;
    }
    printf("[PARENT] FAIL: limit not observed (exit status %d)\n", st == -1 ? -1 : WEXITSTATUS(st));
    return 1;
}

static int test_cpu(void)
{
    double t;
    int st = run_child(child_cpu, &t);

    if (st != -1 && WIFSIGNALED(st) && WTERMSIG(st) == SIGXCPU) {
        printf("[PARENT] child was killed by signal %d (%s) after about %.1f s\n", WTERMSIG(st),
               strsignal(WTERMSIG(st)), t);
        printf("[PARENT] OBSERVED EFFECT: the kernel stopped the busy loop when it used its 1 s CPU\n"
               "         budget; without the limit it would have spun forever.\n");
        show_parent_limit(RLIMIT_CPU, "RLIMIT_CPU");
        return 0;
    }
    if (st != -1 && WIFSIGNALED(st))
        printf("[PARENT] FAIL: child ended by signal %d (%s) - the CPU limit was not enforced here\n",
               WTERMSIG(st), strsignal(WTERMSIG(st)));
    else
        printf("[PARENT] FAIL: unexpected child status\n");
    return 1;
}

static int limits_usage(void)
{
    fprintf(stderr, "Usage: campuscompute limits [files|mem|cpu|all]   (default: all)\n");
    return 2;
}

int cmd_limits(int argc, char **argv)
{
    const char *which = "all";
    int rc = 0, all;

    if (argc > 1)
        return limits_usage();
    if (argc == 1)
        which = argv[0];
    if (strcmp(which, "files") && strcmp(which, "mem") && strcmp(which, "cpu") && strcmp(which, "all")) {
        fprintf(stderr, "Error: unknown limit test '%s'\n", which);
        return limits_usage();
    }
    all = strcmp(which, "all") == 0;

    if (all || strcmp(which, "files") == 0) {
        printf("\n[Test] RLIMIT_NOFILE - maximum open file descriptors\n");
        rc |= test_files();
    }
    if (all || strcmp(which, "mem") == 0) {
        printf("\n[Test] RLIMIT_AS - maximum address space (memory)\n");
        rc |= test_memory();
    }
    if (all || strcmp(which, "cpu") == 0) {
        printf("\n[Test] RLIMIT_CPU - maximum CPU seconds\n");
        rc |= test_cpu();
    }

    printf("\nRESULT: %s\n", rc == 0 ? "PASS - every applied limit had the expected effect"
                                     : "FAIL - a limit did not behave as expected");
    return rc;
}
