/*
 * ipc.c - FR4: POSIX shared memory + named semaphores between two processes
 *         cmd_verify: checks that nothing created by the program is left over
 *
 * Protocol (single-slot mailbox):
 *   semaphore "empty" (initial 1): the slot may be written
 *   semaphore "full"  (initial 0): the slot holds a message to read
 *   CHILD  (producer): wait(empty) -> write message -> post(full)
 *   PARENT (consumer): wait(full)  -> read message  -> post(empty)
 *
 * The child opens the objects BY NAME (shm_open / sem_open), exactly as an
 * unrelated cooperating process would. All names carry the user ID so they
 * cannot collide with another user's objects, and only these three names are
 * ever unlinked by this program.
 */
#include "campuscompute.h"
#include <dirent.h>
#include <fcntl.h>
#include <semaphore.h>
#include <signal.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define IPC_TIMEOUT_S 5

typedef struct {
    int seq;
    int job_id;
    unsigned long checksum;
    char text[64];
} ShmMessage;

static char g_shm_name[64], g_sem_empty[64], g_sem_full[64];

static void build_names(void)
{
    unsigned uid = (unsigned)getuid();

    snprintf(g_shm_name,  sizeof g_shm_name,  "/campuscompute_shm_%u",   uid);
    snprintf(g_sem_empty, sizeof g_sem_empty, "/campuscompute_empty_%u", uid);
    snprintf(g_sem_full,  sizeof g_sem_full,  "/campuscompute_full_%u",  uid);
}

/* The "work" whose result travels through shared memory; both sides can recompute it. */
static unsigned long job_checksum(int job_id, int rounds)
{
    unsigned long h = (unsigned long)job_id;

    for (int r = 0; r < rounds; r++)
        h = (h * 31UL + (unsigned long)r + (unsigned long)job_id) % 1000003UL;
    return h;
}

/* sem_wait with a time-out so a dead peer can never hang the program. */
static int sem_wait_timeout(sem_t *s, int seconds)
{
    struct timespec ts;
    int r;

    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec += seconds;
    while ((r = sem_timedwait(s, &ts)) == -1 && errno == EINTR)
        ;
    return r;
}

/* Remove only the objects this program creates. */
static void unlink_all(int verbose)
{
    if (sem_unlink(g_sem_empty) == 0) {
        if (verbose) printf("[CLEANUP] sem_unlink(%s): removed\n", g_sem_empty);
    } else if (errno != ENOENT) {
        perror("sem_unlink(empty)");
    }
    if (sem_unlink(g_sem_full) == 0) {
        if (verbose) printf("[CLEANUP] sem_unlink(%s): removed\n", g_sem_full);
    } else if (errno != ENOENT) {
        perror("sem_unlink(full)");
    }
    if (shm_unlink(g_shm_name) == 0) {
        if (verbose) printf("[CLEANUP] shm_unlink(%s): removed\n", g_shm_name);
    } else if (errno != ENOENT) {
        perror("shm_unlink");
    }
}

/* Body of the child (producer) process. Never returns. */
static void producer_process(int count)
{
    int fd = shm_open(g_shm_name, O_RDWR, 0);
    ShmMessage *msg;
    sem_t *empty, *full;

    if (fd < 0) { perror("[CHILD] shm_open"); _exit(1); }
    msg = mmap(NULL, sizeof *msg, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (msg == MAP_FAILED) { perror("[CHILD] mmap"); _exit(1); }
    close(fd);

    empty = sem_open(g_sem_empty, 0);
    full  = sem_open(g_sem_full, 0);
    if (empty == SEM_FAILED || full == SEM_FAILED) { perror("[CHILD] sem_open"); _exit(1); }

    for (int i = 1; i <= count; i++) {
        if (sem_wait_timeout(empty, IPC_TIMEOUT_S) != 0) {
            perror("[CHILD] sem_wait(empty)");
            _exit(1);
        }
        msg->seq = i;
        msg->job_id = 100 + i;
        msg->checksum = job_checksum(msg->job_id, 20000 * i);
        snprintf(msg->text, sizeof msg->text, "worker pid %d finished J%d",
                 (int)getpid(), msg->job_id);
        printf("[CHILD  pid=%d] wrote message %d into shared memory\n", (int)getpid(), i);
        fflush(stdout);
        sem_post(full);
    }

    sem_close(empty);
    sem_close(full);
    munmap(msg, sizeof *msg);
    _exit(0);
}

static int ipc_usage(void)
{
    fprintf(stderr, "Usage: campuscompute ipc [MESSAGES]\n  MESSAGES 1..50 (default 5)\n");
    return 2;
}

int cmd_ipc(int argc, char **argv)
{
    int count = 5, rc = 1, bad = 0, mismatches = 0, received = 0, fd = -1, status;
    ShmMessage *msg = MAP_FAILED;
    sem_t *empty = SEM_FAILED, *full = SEM_FAILED;
    pid_t pid = -1;

    if (argc > 1)
        return ipc_usage();
    if (argc == 1 && parse_int(argv[0], 1, 50, &count) != 0) {
        fprintf(stderr, "Error: invalid message count '%s'\n", argv[0]);
        return ipc_usage();
    }

    build_names();
    unlink_all(0);          /* remove a stale copy left by an earlier crashed run of this program */

    fd = shm_open(g_shm_name, O_CREAT | O_EXCL | O_RDWR, 0600);
    if (fd < 0) { perror("shm_open"); goto cleanup; }
    if (ftruncate(fd, (off_t)sizeof(ShmMessage)) != 0) { perror("ftruncate"); goto cleanup; }
    msg = mmap(NULL, sizeof *msg, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (msg == MAP_FAILED) { perror("mmap"); goto cleanup; }
    memset(msg, 0, sizeof *msg);

    empty = sem_open(g_sem_empty, O_CREAT | O_EXCL, 0600, 1);
    full  = sem_open(g_sem_full,  O_CREAT | O_EXCL, 0600, 0);
    if (empty == SEM_FAILED || full == SEM_FAILED) { perror("sem_open"); goto cleanup; }

    printf("[PARENT] created shared memory %s (%zu bytes)\n", g_shm_name, sizeof(ShmMessage));
    printf("[PARENT] created named semaphores %s (=1) and %s (=0)\n", g_sem_empty, g_sem_full);
    fflush(stdout);

    pid = fork();
    if (pid < 0) { perror("fork"); goto cleanup; }
    if (pid == 0)
        producer_process(count);                 /* child never returns */

    printf("[PARENT pid=%d] forked producer child pid=%d, expecting %d messages\n",
           (int)getpid(), (int)pid, count);

    for (int i = 1; i <= count; i++) {
        ShmMessage copy;
        unsigned long expect;

        if (sem_wait_timeout(full, IPC_TIMEOUT_S) != 0) {
            fprintf(stderr, "Error: timed out waiting for message %d: %s\n", i, strerror(errno));
            bad = 1;
            break;
        }
        copy = *msg;                              /* read while the slot is protected */
        expect = job_checksum(copy.job_id, 20000 * copy.seq);
        received++;
        if (copy.checksum != expect)
            mismatches++;
        printf("[PARENT ] read message %d: J%d checksum=%lu (%s) \"%s\"\n", copy.seq,
               copy.job_id, copy.checksum, copy.checksum == expect ? "verified OK" : "MISMATCH",
               copy.text);
        fflush(stdout);
        sem_post(empty);                          /* slot is free again */
    }

    if (bad)
        kill(pid, SIGTERM);
    while (waitpid(pid, &status, 0) < 0 && errno == EINTR)
        ;
    printf("[PARENT ] child %d %s\n", (int)pid,
           (WIFEXITED(status) && WEXITSTATUS(status) == 0) ? "exited normally (status 0)"
                                                           : "did NOT exit normally");
    if (!bad && mismatches == 0 && received == count && WIFEXITED(status) && WEXITSTATUS(status) == 0) {
        printf("[PARENT ] RESULT: PASS - %d/%d messages exchanged through shared memory\n", received, count);
        rc = 0;
    } else {
        printf("[PARENT ] RESULT: FAIL (received=%d mismatches=%d)\n", received, mismatches);
    }
    pid = -1;

cleanup:
    if (pid > 0) {                                /* error path: do not leave the child running */
        kill(pid, SIGTERM);
        waitpid(pid, NULL, 0);
    }
    if (empty != SEM_FAILED) sem_close(empty);
    if (full  != SEM_FAILED) sem_close(full);
    if (msg   != MAP_FAILED) munmap(msg, sizeof *msg);
    if (fd >= 0) close(fd);
    unlink_all(1);
    return rc;
}

/* ------------------------------------------------------------------ */
/* Cleanup verification                                                */
/* ------------------------------------------------------------------ */

static int count_threads(void)
{
    DIR *d = opendir("/proc/self/task");
    struct dirent *e;
    int n = 0;

    if (d == NULL)
        return -1;
    while ((e = readdir(d)) != NULL)
        if (e->d_name[0] != '.')
            n++;
    closedir(d);
    return n;
}

int cmd_verify(int argc, char **argv)
{
    int leaks = 0, fd, threads;
    sem_t *s;
    pid_t r;

    (void)argv;
    if (argc != 0) {
        fprintf(stderr, "Usage: campuscompute verify\n");
        return 2;
    }
    build_names();

    printf("Checking resources created by CampusCompute:\n");

    fd = shm_open(g_shm_name, O_RDONLY, 0);
    if (fd >= 0) { close(fd); printf("  shared memory %-32s : STILL PRESENT\n", g_shm_name); leaks++; }
    else if (errno == ENOENT) printf("  shared memory %-32s : released\n", g_shm_name);
    else { printf("  shared memory %-32s : check failed (%s)\n", g_shm_name, strerror(errno)); leaks++; }

    s = sem_open(g_sem_empty, 0);
    if (s != SEM_FAILED) { sem_close(s); printf("  semaphore     %-32s : STILL PRESENT\n", g_sem_empty); leaks++; }
    else if (errno == ENOENT) printf("  semaphore     %-32s : released\n", g_sem_empty);
    else { printf("  semaphore     %-32s : check failed (%s)\n", g_sem_empty, strerror(errno)); leaks++; }

    s = sem_open(g_sem_full, 0);
    if (s != SEM_FAILED) { sem_close(s); printf("  semaphore     %-32s : STILL PRESENT\n", g_sem_full); leaks++; }
    else if (errno == ENOENT) printf("  semaphore     %-32s : released\n", g_sem_full);
    else { printf("  semaphore     %-32s : check failed (%s)\n", g_sem_full, strerror(errno)); leaks++; }

    r = waitpid(-1, NULL, WNOHANG);
    if (r == -1 && errno == ECHILD)
        printf("  child processes                                      : none left (all reaped)\n");
    else { printf("  child processes                                      : a child is still unreaped\n"); leaks++; }

    threads = count_threads();
    if (threads == 1)
        printf("  threads in this process                              : 1 (main thread only)\n");
    else if (threads < 0)
        printf("  threads in this process                              : unavailable (/proc not readable)\n");
    else { printf("  threads in this process                              : %d (expected 1)\n", threads); leaks++; }

    printf("  log file 'campuscompute.log' is an output file and is kept for inspection.\n");
    printf("RESULT: %s\n", leaks == 0 ? "PASS - nothing created by the program is left behind"
                                      : "FAIL - leftovers found");
    return leaks == 0 ? 0 : 1;
}
