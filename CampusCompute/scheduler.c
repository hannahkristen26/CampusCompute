/*
 * scheduler.c - FR1 (job management) and FR2 (CPU scheduling)
 *
 * Policies: FCFS, Round Robin, non-preemptive Priority.
 *
 * TIE-BREAKING RULES (also printed with every result)
 *   FCFS     : earlier arrival first; equal arrival -> lower job ID first.
 *   Priority : lower priority number = higher priority; equal priority ->
 *              earlier arrival; still equal -> lower job ID.
 *   RR       : ready queue is FIFO. A job that arrives at the same instant a
 *              quantum expires is queued BEFORE the preempted job. Jobs with
 *              equal arrival are queued by lower job ID.
 */
#include "campuscompute.h"
#include <ctype.h>

typedef enum { POLICY_FCFS, POLICY_RR, POLICY_PRIORITY } Policy;

/* ------------------------------------------------------------------ */
/* FR1: job input and validation                                       */
/* ------------------------------------------------------------------ */

static const char *status_name(JobStatus s)
{
    switch (s) {
    case JOB_NEW:       return "NEW";
    case JOB_READY:     return "READY";
    case JOB_RUNNING:   return "RUNNING";
    case JOB_COMPLETED: return "COMPLETED";
    }
    return "?";
}

static void init_runtime(Job *j)
{
    j->status     = JOB_NEW;
    j->remaining  = j->burst;
    j->start_time = -1;
    j->completion = -1;
    j->turnaround = -1;
    j->waiting    = -1;
}

/* Built-in sample set: includes an arrival tie (J4,J5) and a priority tie (J2,J5). */
static void load_builtin_jobs(Job *jobs, int *n)
{
    static const int data[][4] = {
        /* id arrival burst priority */
        {1, 0, 5, 3}, {2, 1, 3, 1}, {3, 2, 8, 4}, {4, 3, 6, 2}, {5, 3, 2, 1}
    };

    *n = (int)(sizeof data / sizeof data[0]);
    for (int i = 0; i < *n; i++) {
        jobs[i].id       = data[i][0];
        jobs[i].arrival  = data[i][1];
        jobs[i].burst    = data[i][2];
        jobs[i].priority = data[i][3];
        init_runtime(&jobs[i]);
    }
}

/* Load "id arrival burst priority" lines from a file. Returns 0 on success. */
static int load_jobs_file(const char *path, Job *jobs, int *n)
{
    char line[256];
    int lineno = 0;
    FILE *fp = fopen(path, "r");

    if (fp == NULL) {
        fprintf(stderr, "Error: cannot open jobs file '%s': %s\n", path, strerror(errno));
        return -1;
    }

    *n = 0;
    while (fgets(line, sizeof line, fp) != NULL) {
        char *hash = strchr(line, '#');
        char *p = line;
        long v[4];
        const char *why = NULL;

        lineno++;
        if (hash != NULL)
            *hash = '\0';
        while (isspace((unsigned char)*p))
            p++;
        if (*p == '\0')
            continue;                       /* blank / comment-only line */

        if (parse_int_line(p, v, 4) != 0)
            why = "expected four integers: id arrival burst priority";
        else if (v[0] < 1 || v[0] > 9999)
            why = "job ID must be 1..9999";
        else if (v[1] < 0 || v[1] > 1000)
            why = "arrival time must be 0..1000";
        else if (v[2] < 1 || v[2] > 100)
            why = "CPU burst must be 1..100";
        else if (v[3] < 1 || v[3] > 10)
            why = "priority must be 1..10";
        else if (*n >= MAX_JOBS)
            why = "too many jobs (maximum is 32)";
        else {
            for (int i = 0; i < *n; i++)
                if (jobs[i].id == (int)v[0])
                    why = "duplicate job ID";
        }

        if (why != NULL) {
            fprintf(stderr, "Error: '%s' line %d: %s\n", path, lineno, why);
            fclose(fp);
            return -1;
        }

        jobs[*n].id       = (int)v[0];
        jobs[*n].arrival  = (int)v[1];
        jobs[*n].burst    = (int)v[2];
        jobs[*n].priority = (int)v[3];
        init_runtime(&jobs[*n]);
        (*n)++;
    }
    fclose(fp);

    if (*n == 0) {
        fprintf(stderr, "Error: jobs file '%s' contains no jobs\n", path);
        return -1;
    }
    return 0;
}

static void print_job_input(const Job *jobs, int n)
{
    printf("Job set (%d jobs):\n", n);
    printf("  %-6s %-8s %-6s %-8s %-10s\n", "Job", "Arrival", "Burst", "Priority", "Status");
    for (int i = 0; i < n; i++)
        printf("  J%-5d %-8d %-6d %-8d %-10s\n", jobs[i].id, jobs[i].arrival,
               jobs[i].burst, jobs[i].priority, status_name(jobs[i].status));
}

/* ------------------------------------------------------------------ */
/* Timeline helpers                                                    */
/* ------------------------------------------------------------------ */

/* Append a slice; adjacent slices of the same job are merged. */
static void tl_add(Timeline *tl, int job_id, int start, int end)
{
    if (end <= start)
        return;
    if (tl->count > 0) {
        Segment *last = &tl->seg[tl->count - 1];
        if (last->job_id == job_id && last->end == start) {
            last->end = end;
            return;
        }
    }
    if (tl->count < MAX_SEGMENTS) {
        tl->seg[tl->count].job_id = job_id;
        tl->seg[tl->count].start  = start;
        tl->seg[tl->count].end    = end;
        tl->count++;
    }
}

/* Index list of jobs ordered by (arrival, id). */
static void sort_by_arrival(const Job *jobs, int n, int *order)
{
    for (int i = 0; i < n; i++) {
        int k = i;
        while (k > 0) {
            const Job *a = &jobs[order[k - 1]];
            const Job *b = &jobs[i];
            if (a->arrival < b->arrival || (a->arrival == b->arrival && a->id < b->id))
                break;
            order[k] = order[k - 1];
            k--;
        }
        order[k] = i;
    }
}

/* ------------------------------------------------------------------ */
/* FR2: scheduling algorithms (each returns the finish time)           */
/* ------------------------------------------------------------------ */

static int run_fcfs(Job *jobs, int n, Timeline *tl)
{
    int order[MAX_JOBS];
    int time = 0;

    sort_by_arrival(jobs, n, order);
    for (int k = 0; k < n; k++) {
        Job *j = &jobs[order[k]];

        if (time < j->arrival) {                    /* CPU idle until it arrives */
            tl_add(tl, -1, time, j->arrival);
            time = j->arrival;
        }
        j->status = JOB_RUNNING;
        j->start_time = time;
        tl_add(tl, j->id, time, time + j->burst);
        time += j->burst;
        j->remaining = 0;
        j->completion = time;
        j->status = JOB_COMPLETED;
    }
    return time;
}

/* Move every job that has arrived by 'time' into the ready queue. */
static void admit_arrivals(Job *jobs, const int *order, int n, int *next, int time,
                           int *queue, int *tail, int *qcount)
{
    while (*next < n && jobs[order[*next]].arrival <= time) {
        jobs[order[*next]].status = JOB_READY;
        queue[*tail] = order[*next];
        *tail = (*tail + 1) % MAX_JOBS;
        (*qcount)++;
        (*next)++;
    }
}

static int run_rr(Job *jobs, int n, int quantum, Timeline *tl)
{
    int order[MAX_JOBS], queue[MAX_JOBS];
    int head = 0, tail = 0, qcount = 0;
    int next = 0, done = 0, time = 0;

    sort_by_arrival(jobs, n, order);

    while (done < n) {
        Job *j;
        int idx, slice;

        admit_arrivals(jobs, order, n, &next, time, queue, &tail, &qcount);

        if (qcount == 0) {                          /* nothing ready: CPU idles */
            int t = jobs[order[next]].arrival;      /* next < n is guaranteed   */
            tl_add(tl, -1, time, t);
            time = t;
            continue;
        }

        idx = queue[head];
        head = (head + 1) % MAX_JOBS;
        qcount--;
        j = &jobs[idx];

        if (j->start_time < 0)
            j->start_time = time;
        j->status = JOB_RUNNING;

        slice = (j->remaining < quantum) ? j->remaining : quantum;
        tl_add(tl, j->id, time, time + slice);
        time += slice;
        j->remaining -= slice;

        /* New arrivals (including those arriving exactly now) go first. */
        admit_arrivals(jobs, order, n, &next, time, queue, &tail, &qcount);

        if (j->remaining > 0) {
            j->status = JOB_READY;
            queue[tail] = idx;
            tail = (tail + 1) % MAX_JOBS;
            qcount++;
        } else {
            j->status = JOB_COMPLETED;
            j->completion = time;
            done++;
        }
    }
    return time;
}

/* Is job a a better choice than job b under priority scheduling? */
static int better_priority(const Job *a, const Job *b)
{
    if (a->priority != b->priority)
        return a->priority < b->priority;
    if (a->arrival != b->arrival)
        return a->arrival < b->arrival;
    return a->id < b->id;
}

static int run_priority(Job *jobs, int n, Timeline *tl)
{
    int done = 0, time = 0;

    while (done < n) {
        int best = -1;

        for (int i = 0; i < n; i++) {
            if (jobs[i].status == JOB_COMPLETED || jobs[i].arrival > time)
                continue;
            if (best < 0 || better_priority(&jobs[i], &jobs[best]))
                best = i;
        }

        if (best < 0) {                             /* nobody has arrived yet */
            int t = -1;
            for (int i = 0; i < n; i++)
                if (jobs[i].status != JOB_COMPLETED && (t < 0 || jobs[i].arrival < t))
                    t = jobs[i].arrival;
            tl_add(tl, -1, time, t);
            time = t;
            continue;
        }

        jobs[best].status = JOB_RUNNING;
        jobs[best].start_time = time;
        tl_add(tl, jobs[best].id, time, time + jobs[best].burst);
        time += jobs[best].burst;
        jobs[best].remaining = 0;
        jobs[best].completion = time;
        jobs[best].status = JOB_COMPLETED;
        done++;
    }
    return time;
}

/* ------------------------------------------------------------------ */
/* Metrics and reporting                                               */
/* ------------------------------------------------------------------ */

static void compute_stats(Job *jobs, int n, const Timeline *tl, SchedStats *st)
{
    double sum_ct = 0, sum_tat = 0, sum_wt = 0;
    int busy = 0, finish = 0;

    for (int i = 0; i < n; i++) {
        jobs[i].turnaround = jobs[i].completion - jobs[i].arrival;
        jobs[i].waiting    = jobs[i].turnaround - jobs[i].burst;
        sum_ct  += jobs[i].completion;
        sum_tat += jobs[i].turnaround;
        sum_wt  += jobs[i].waiting;
        busy    += jobs[i].burst;
        if (jobs[i].completion > finish)
            finish = jobs[i].completion;
    }

    st->avg_completion = sum_ct / n;
    st->avg_turnaround = sum_tat / n;
    st->avg_waiting    = sum_wt / n;
    st->total_time     = finish;
    st->cpu_util       = finish > 0 ? 100.0 * busy / finish : 0.0;

    st->switches = 0;
    for (int i = 1; i < tl->count; i++)
        if (tl->seg[i].job_id >= 0 && tl->seg[i - 1].job_id >= 0)
            st->switches++;
}

static void print_gantt(const Timeline *tl)
{
    const int per_row = 8;

    printf("Gantt chart (-- = CPU idle):\n");
    for (int row = 0; row < tl->count; row += per_row) {
        int last = (row + per_row < tl->count) ? row + per_row : tl->count;

        printf("  ");
        for (int i = row; i < last; i++) {
            if (tl->seg[i].job_id < 0)
                printf("| --    ");
            else
                printf("| J%-4d ", tl->seg[i].job_id);
        }
        printf("|\n  ");
        for (int i = row; i < last; i++)
            printf("%-8d", tl->seg[i].start);
        printf("%d\n", tl->seg[last - 1].end);
    }
}

static void print_order(const Timeline *tl)
{
    int first = 1;

    printf("Execution order: ");
    for (int i = 0; i < tl->count; i++) {
        if (tl->seg[i].job_id < 0)
            continue;
        printf("%sJ%d", first ? "" : " -> ", tl->seg[i].job_id);
        first = 0;
    }
    printf("\n");
}

static void print_results(const char *title, const char *rule, const Job *jobs, int n,
                          const Timeline *tl, const SchedStats *st)
{
    printf("\n--- %s ---\n", title);
    printf("%s\n", rule);
    print_gantt(tl);
    print_order(tl);

    printf("\n  %-6s %-8s %-6s %-8s %-11s %-11s %-8s %-10s\n", "Job", "Arrival", "Burst",
           "Priority", "Completion", "Turnaround", "Waiting", "Status");
    for (int i = 0; i < n; i++)
        printf("  J%-5d %-8d %-6d %-8d %-11d %-11d %-8d %-10s\n", jobs[i].id,
               jobs[i].arrival, jobs[i].burst, jobs[i].priority, jobs[i].completion,
               jobs[i].turnaround, jobs[i].waiting, status_name(jobs[i].status));

    printf("\n  Average completion time : %.2f\n", st->avg_completion);
    printf("  Average turnaround time : %.2f\n", st->avg_turnaround);
    printf("  Average waiting time    : %.2f\n", st->avg_waiting);
    printf("  CPU utilisation         : %.1f%%   (finish time %d)\n", st->cpu_util, st->total_time);
}

static const char *policy_name(Policy p, char *buf, size_t len, int quantum)
{
    switch (p) {
    case POLICY_FCFS:     snprintf(buf, len, "FCFS"); break;
    case POLICY_RR:       snprintf(buf, len, "Round Robin (q=%d)", quantum); break;
    case POLICY_PRIORITY: snprintf(buf, len, "Priority (non-preemptive)"); break;
    }
    return buf;
}

static const char *policy_rule(Policy p, char *buf, size_t len, int quantum)
{
    switch (p) {
    case POLICY_FCFS:
        snprintf(buf, len, "Tie-breaking: earlier arrival first; equal arrival -> lower job ID first.");
        break;
    case POLICY_RR:
        snprintf(buf, len,
                 "Time quantum = %d. Tie-breaking: FIFO ready queue; a job arriving exactly when a quantum\n"
                 "expires is queued BEFORE the preempted job; equal arrivals -> lower job ID first.",
                 quantum);
        break;
    case POLICY_PRIORITY:
        snprintf(buf, len,
                 "Lower number = higher priority. Tie-breaking: equal priority -> earlier arrival;\n"
                 "still equal -> lower job ID.");
        break;
    }
    return buf;
}

/* Run one policy on a private copy of the job set. */
static void run_policy(Policy p, const Job *orig, int n, int quantum,
                       Job *out, Timeline *tl, SchedStats *st)
{
    memcpy(out, orig, (size_t)n * sizeof(Job));
    for (int i = 0; i < n; i++)
        init_runtime(&out[i]);
    tl->count = 0;

    switch (p) {
    case POLICY_FCFS:     run_fcfs(out, n, tl);            break;
    case POLICY_RR:       run_rr(out, n, quantum, tl);     break;
    case POLICY_PRIORITY: run_priority(out, n, tl);        break;
    }
    compute_stats(out, n, tl, st);
}

static void report_policy(Policy p, const Job *orig, int n, int quantum)
{
    Job work[MAX_JOBS];
    Timeline tl;
    SchedStats st;
    char name[64], rule[300];

    run_policy(p, orig, n, quantum, work, &tl, &st);
    print_results(policy_name(p, name, sizeof name, quantum),
                  policy_rule(p, rule, sizeof rule, quantum), work, n, &tl, &st);
}

static void report_comparison(const Job *orig, int n, int quantum)
{
    static const Policy all[3] = { POLICY_FCFS, POLICY_RR, POLICY_PRIORITY };
    Job work[MAX_JOBS];
    Timeline tl;
    SchedStats st[3];
    char name[3][64], buf[64];
    int best_wt = 0, best_tat = 0;

    printf("\n--- Scheduling policy comparison (same job set) ---\n");
    for (int k = 0; k < 3; k++) {
        run_policy(all[k], orig, n, quantum, work, &tl, &st[k]);
        policy_name(all[k], name[k], sizeof name[k], quantum);
        printf("  %-26s order: ", name[k]);
        for (int i = 0, first = 1; i < tl.count; i++) {
            if (tl.seg[i].job_id < 0)
                continue;
            printf("%sJ%d", first ? "" : " ", tl.seg[i].job_id);
            first = 0;
        }
        printf("\n");
        if (st[k].avg_waiting < st[best_wt].avg_waiting)
            best_wt = k;
        if (st[k].avg_turnaround < st[best_tat].avg_turnaround)
            best_tat = k;
    }

    printf("\n  %-26s %-10s %-10s %-10s %-9s %-8s\n", "Policy", "Avg CT", "Avg TAT",
           "Avg WT", "Switches", "CPU %");
    for (int k = 0; k < 3; k++)
        printf("  %-26s %-10.2f %-10.2f %-10.2f %-9d %-8.1f\n", name[k], st[k].avg_completion,
               st[k].avg_turnaround, st[k].avg_waiting, st[k].switches, st[k].cpu_util);

    printf("\n  Lowest average waiting time    : %s\n", policy_name(all[best_wt], buf, sizeof buf, quantum));
    printf("  Lowest average turnaround time : %s\n", policy_name(all[best_tat], buf, sizeof buf, quantum));
    printf("  (Switches = direct job-to-job CPU hand-overs in the Gantt chart)\n");
}

/* ------------------------------------------------------------------ */
/* Command entry point                                                 */
/* ------------------------------------------------------------------ */

static int sched_usage(void)
{
    fprintf(stderr, "Usage: campuscompute sched <fcfs|rr|priority|compare> [-f JOBFILE] [-q QUANTUM]\n"
                    "  -f JOBFILE  load jobs from a file (default: built-in sample set)\n"
                    "  -q QUANTUM  Round Robin time quantum, 1..100 (default %d)\n", DEFAULT_QUANTUM);
    return 2;
}

int cmd_sched(int argc, char **argv)
{
    Job jobs[MAX_JOBS];
    int n = 0, quantum = DEFAULT_QUANTUM;
    const char *file = NULL, *algo;

    if (argc < 1)
        return sched_usage();
    algo = argv[0];

    if (strcmp(algo, "fcfs") && strcmp(algo, "rr") && strcmp(algo, "priority") &&
        strcmp(algo, "compare")) {
        fprintf(stderr, "Error: unknown scheduling policy '%s'\n", algo);
        return sched_usage();
    }

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-f") == 0 && i + 1 < argc) {
            file = argv[++i];
        } else if (strcmp(argv[i], "-q") == 0 && i + 1 < argc) {
            if (parse_int(argv[++i], 1, 100, &quantum) != 0) {
                fprintf(stderr, "Error: invalid time quantum '%s' (expected an integer 1..100)\n", argv[i]);
                return 2;
            }
        } else {
            fprintf(stderr, "Error: unexpected argument '%s'\n", argv[i]);
            return sched_usage();
        }
    }

    if (file != NULL) {
        if (load_jobs_file(file, jobs, &n) != 0)
            return 2;
        printf("Jobs loaded from '%s'.\n", file);
    } else {
        load_builtin_jobs(jobs, &n);
        printf("Using the built-in sample job set (use -f FILE to load your own).\n");
    }
    print_job_input(jobs, n);

    if (strcmp(algo, "fcfs") == 0)
        report_policy(POLICY_FCFS, jobs, n, quantum);
    else if (strcmp(algo, "rr") == 0)
        report_policy(POLICY_RR, jobs, n, quantum);
    else if (strcmp(algo, "priority") == 0)
        report_policy(POLICY_PRIORITY, jobs, n, quantum);
    else
        report_comparison(jobs, n, quantum);

    return 0;
}
