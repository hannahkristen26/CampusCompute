/*
 * banker.c - FR5: Banker's safety algorithm
 *
 *   Need[i][j] = Maximum[i][j] - Allocation[i][j]
 *
 *   Work = Available; Finish[i] = false
 *   repeat: find an unfinished process i with Need[i] <= Work,
 *           then Work += Allocation[i] and mark i finished (it can run to
 *           completion and release everything it holds).
 *   If every process finishes the state is SAFE and the order is a safe
 *   sequence; otherwise the state is UNSAFE.
 *
 * Input: built-in cases ("safe" / "unsafe") or a text file:
 *     n m
 *     Available (m numbers)
 *     Maximum    (n rows of m numbers)
 *     Allocation (n rows of m numbers)
 *   '#' starts a comment.
 */
#include "campuscompute.h"

#define MAXP 10
#define MAXR 10

typedef struct {
    int n, m;
    int available[MAXR];
    int max[MAXP][MAXR];
    int alloc[MAXP][MAXR];
    int need[MAXP][MAXR];
} BankerState;

/* Textbook example (5 processes, 3 resource types). */
static const int B_MAX[5][3]   = { {7,5,3}, {3,2,2}, {9,0,2}, {2,2,2}, {4,3,3} };
static const int B_ALLOC_SAFE[5][3]   = { {0,1,0}, {2,0,0}, {3,0,2}, {2,1,1}, {0,0,2} };
static const int B_ALLOC_UNSAFE[5][3] = { {0,3,0}, {3,0,2}, {3,0,2}, {2,1,1}, {0,0,2} };

static void load_builtin(BankerState *s, int safe_case)
{
    static const int avail_safe[3]   = {3, 3, 2};
    static const int avail_unsafe[3] = {2, 1, 0};

    memset(s, 0, sizeof *s);
    s->n = 5;
    s->m = 3;
    for (int j = 0; j < 3; j++)
        s->available[j] = safe_case ? avail_safe[j] : avail_unsafe[j];
    for (int i = 0; i < 5; i++)
        for (int j = 0; j < 3; j++) {
            s->max[i][j]   = B_MAX[i][j];
            s->alloc[i][j] = safe_case ? B_ALLOC_SAFE[i][j] : B_ALLOC_UNSAFE[i][j];
        }
}

static int load_state_file(const char *path, BankerState *s)
{
    long v[2 + MAXR + 2 * MAXP * MAXR];
    int count, pos = 0, n, m;

    if (load_int_tokens(path, v, (int)(sizeof v / sizeof v[0]), &count) != 0)
        return -1;
    if (count < 2) {
        fprintf(stderr, "Error: '%s': expected 'n m' on the first line\n", path);
        return -1;
    }
    if (v[0] < 1 || v[0] > MAXP || v[1] < 1 || v[1] > MAXR) {
        fprintf(stderr, "Error: '%s': need 1..%d processes and 1..%d resource types\n", path, MAXP, MAXR);
        return -1;
    }
    n = (int)v[0];
    m = (int)v[1];
    if (count != 2 + m + 2 * n * m) {
        fprintf(stderr, "Error: '%s': expected %d numbers (n m, Available, Maximum, Allocation) but found %d\n",
                path, 2 + m + 2 * n * m, count);
        return -1;
    }

    memset(s, 0, sizeof *s);
    s->n = n;
    s->m = m;
    pos = 2;
    for (int j = 0; j < m; j++)
        s->available[j] = (int)v[pos++];
    for (int i = 0; i < n; i++)
        for (int j = 0; j < m; j++)
            s->max[i][j] = (int)v[pos++];
    for (int i = 0; i < n; i++)
        for (int j = 0; j < m; j++)
            s->alloc[i][j] = (int)v[pos++];
    return 0;
}

/* Validate the data and compute Need = Maximum - Allocation. */
static int compute_need(BankerState *s)
{
    for (int j = 0; j < s->m; j++)
        if (s->available[j] < 0) {
            fprintf(stderr, "Error: Available[%c] is negative\n", 'A' + j);
            return -1;
        }
    for (int i = 0; i < s->n; i++)
        for (int j = 0; j < s->m; j++) {
            if (s->max[i][j] < 0 || s->alloc[i][j] < 0) {
                fprintf(stderr, "Error: negative value for P%d, resource %c\n", i, 'A' + j);
                return -1;
            }
            if (s->alloc[i][j] > s->max[i][j]) {
                fprintf(stderr, "Error: P%d holds more of resource %c than its maximum claim\n", i, 'A' + j);
                return -1;
            }
            s->need[i][j] = s->max[i][j] - s->alloc[i][j];
        }
    return 0;
}

static void print_vec(const int *v, int m)
{
    printf("[");
    for (int j = 0; j < m; j++)
        printf("%s%d", j ? " " : "", v[j]);
    printf("]");
}

/* Print one matrix row padded to 'width' characters. */
static void print_row(const int *v, int m, int width)
{
    for (int j = 0; j < m; j++)
        printf("%-3d", v[j]);
    printf("%*s", width - 3 * m, "");
}

static void print_state(const BankerState *s)
{
    int w = (3 * s->m > 10) ? 3 * s->m : 10;       /* block width, wide enough for "Allocation" */

    printf("Processes: %d   Resource types: %d   Available: ", s->n, s->m);
    print_vec(s->available, s->m);

    printf("\n\n  %-5s%-*s   %-*s   %-*s\n", "", w, "Allocation", w, "Maximum", w, "Need");
    printf("  %-5s", "");
    for (int pass = 0; pass < 3; pass++) {
        for (int j = 0; j < s->m; j++)
            printf("%-3c", 'A' + j);
        printf("%*s   ", w - 3 * s->m, "");
    }
    printf("\n");
    for (int i = 0; i < s->n; i++) {
        printf("  P%-4d", i);
        print_row(s->alloc[i], s->m, w);
        printf("   ");
        print_row(s->max[i], s->m, w);
        printf("   ");
        print_row(s->need[i], s->m, w);
        printf("\n");
    }
}

/* The safety algorithm with a step-by-step trace. Returns 1 if safe. */
static int is_safe(const BankerState *s, int *seq)
{
    int work[MAXR], finish[MAXP] = {0};
    int count = 0, progress = 1;

    memcpy(work, s->available, sizeof work);
    printf("\nSafety algorithm trace (Work starts as Available):\n");

    while (count < s->n && progress) {
        progress = 0;
        for (int i = 0; i < s->n; i++) {
            int ok = 1, before[MAXR];

            if (finish[i])
                continue;
            for (int j = 0; j < s->m; j++)
                if (s->need[i][j] > work[j]) {
                    ok = 0;
                    break;
                }
            if (!ok)
                continue;

            memcpy(before, work, sizeof before);
            for (int j = 0; j < s->m; j++)
                work[j] += s->alloc[i][j];
            finish[i] = 1;
            seq[count++] = i;
            progress = 1;

            printf("  Step %d: P%d can finish, Need ", count, i);
            print_vec(s->need[i], s->m);
            printf(" <= Work ");
            print_vec(before, s->m);
            printf("; it releases ");
            print_vec(s->alloc[i], s->m);
            printf(" -> Work ");
            print_vec(work, s->m);
            printf("\n");
        }
    }

    if (count == s->n)
        return 1;

    printf("  No unfinished process has Need <= Work ");
    print_vec(work, s->m);
    printf("\n  Stuck processes:");
    for (int i = 0; i < s->n; i++)
        if (!finish[i])
            printf(" P%d", i);
    printf("\n");
    return 0;
}

/* Analyse one state and print the verdict. Returns 1 safe, 0 unsafe, -1 invalid input. */
static int analyse(const char *title, BankerState *s)
{
    int seq[MAXP];

    if (compute_need(s) != 0)
        return -1;

    printf("Banker's algorithm - %s\n", title);
    print_state(s);

    if (is_safe(s, seq)) {
        printf("\nRESULT: SAFE state. Safe sequence: <");
        for (int i = 0; i < s->n; i++)
            printf("%sP%d", i ? ", " : "", seq[i]);
        printf(">\n");
        return 1;
    }
    printf("\nRESULT: UNSAFE state - no safe sequence exists; deadlock is possible.\n");
    return 0;
}

int banker_run_case(int safe_case)
{
    BankerState s;

    load_builtin(&s, safe_case);
    return analyse(safe_case ? "built-in SAFE case" : "built-in UNSAFE case", &s);
}

int cmd_banker(int argc, char **argv)
{
    BankerState s;
    int r;

    if (argc != 1) {
        fprintf(stderr, "Usage: campuscompute banker <safe|unsafe|FILE>\n");
        return 2;
    }
    if (strcmp(argv[0], "safe") == 0) {
        r = banker_run_case(1);
    } else if (strcmp(argv[0], "unsafe") == 0) {
        r = banker_run_case(0);
    } else {
        if (load_state_file(argv[0], &s) != 0)
            return 2;
        r = analyse(argv[0], &s);
    }
    return r < 0 ? 2 : 0;       /* an UNSAFE verdict is a valid result, not an error */
}
