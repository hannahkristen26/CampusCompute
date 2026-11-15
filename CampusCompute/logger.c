/*
 * logger.c - FR3: concurrent activity logger
 *
 * Several producer threads put log events into a bounded circular queue; one
 * consumer thread takes them out and writes them to campuscompute.log.
 *
 *   - One mutex protects every field of the queue.
 *   - Condition variable not_full  : producers wait here when the queue is full.
 *   - Condition variable not_empty : the consumer waits here when it is empty.
 *   - Shutdown: after all producers are joined, 'producers_finished' is set
 *     under the mutex and not_empty is broadcast; the consumer drains what is
 *     left and exits, then it is joined. No thread is cancelled or leaked.
 */
#include "campuscompute.h"
#include <pthread.h>
#include <time.h>

#define LOG_FILE      "campuscompute.log"
#define LOG_TEXT_LEN  80
#define PRODUCER_DELAY_MS 15     /* producers are fast ...                  */
#define CONSUMER_DELAY_MS 40     /* ... the consumer is slow, so the queue fills */

typedef struct {
    int  producer_id;
    int  seq;                    /* 1..events_per_producer */
    char text[LOG_TEXT_LEN];
} LogEvent;

typedef struct {
    LogEvent *buf;
    int capacity, head, tail, count;
    int producers_finished;      /* no more events will ever arrive */

    pthread_mutex_t lock;
    pthread_cond_t  not_full;
    pthread_cond_t  not_empty;

    FILE *fp;
    int events_per_producer;
    int produced;                /* guarded by lock                     */
    int full_waits, empty_waits; /* guarded by lock                     */
    int consumed;                /* written only by the consumer thread */
} LogQueue;

typedef struct { LogQueue *q; int id; } ProducerArg;

static const char *kinds[] = {
    "submitted a job", "job started", "job completed",
    "requested a resource", "released a resource"
};

/* Format and write one entry to the log file. */
static int write_log_line(FILE *fp, int log_no, const LogEvent *ev)
{
    struct timespec ts;
    struct tm tmv;
    char stamp[32];

    clock_gettime(CLOCK_REALTIME, &ts);
    localtime_r(&ts.tv_sec, &tmv);
    strftime(stamp, sizeof stamp, "%Y-%m-%d %H:%M:%S", &tmv);

    return fprintf(fp, "[%s.%03ld] #%03d P%d/E%d %s\n", stamp, ts.tv_nsec / 1000000L,
                   log_no, ev->producer_id, ev->seq, ev->text);
}

static void *producer_thread(void *arg)
{
    ProducerArg *pa = (ProducerArg *)arg;
    LogQueue *q = pa->q;

    for (int e = 1; e <= q->events_per_producer; e++) {
        LogEvent ev;

        ev.producer_id = pa->id;
        ev.seq = e;
        snprintf(ev.text, sizeof ev.text, "user%d %s (J%d)", pa->id,
                 kinds[(pa->id + e) % 5], pa->id * 100 + e);

        pthread_mutex_lock(&q->lock);
        while (q->count == q->capacity) {                 /* queue FULL: wait */
            q->full_waits++;
            printf("[PRODUCER %d] queue FULL (%d/%d) -> waiting on not_full\n",
                   pa->id, q->count, q->capacity);
            pthread_cond_wait(&q->not_full, &q->lock);
        }
        q->buf[q->tail] = ev;
        q->tail = (q->tail + 1) % q->capacity;
        q->count++;
        q->produced++;
        printf("[PRODUCER %d] queued event %d (queue %d/%d)\n", pa->id, e, q->count, q->capacity);
        pthread_cond_signal(&q->not_empty);
        pthread_mutex_unlock(&q->lock);

        sleep_ms(PRODUCER_DELAY_MS);
    }
    return NULL;
}

static void *consumer_thread(void *arg)
{
    LogQueue *q = (LogQueue *)arg;
    int log_no = 0;

    for (;;) {
        LogEvent ev;

        pthread_mutex_lock(&q->lock);
        while (q->count == 0 && !q->producers_finished) { /* queue EMPTY: wait */
            q->empty_waits++;
            printf("[CONSUMER ] queue EMPTY -> waiting on not_empty\n");
            pthread_cond_wait(&q->not_empty, &q->lock);
        }
        if (q->count == 0 && q->producers_finished) {     /* drained and done  */
            pthread_mutex_unlock(&q->lock);
            break;
        }
        ev = q->buf[q->head];
        q->head = (q->head + 1) % q->capacity;
        q->count--;
        pthread_cond_signal(&q->not_full);
        pthread_mutex_unlock(&q->lock);

        log_no++;
        if (write_log_line(q->fp, log_no, &ev) < 0)
            fprintf(stderr, "[CONSUMER ] warning: failed to write log entry %d\n", log_no);
        q->consumed++;
        printf("[CONSUMER ] wrote log #%d (from producer %d, event %d)\n", log_no,
               ev.producer_id, ev.seq);

        sleep_ms(CONSUMER_DELAY_MS);
    }
    printf("[CONSUMER ] no more events - exiting\n");
    return NULL;
}

/* Count the lines of a text file (used to verify the log really has every entry). */
static int count_lines(const char *path)
{
    FILE *fp = fopen(path, "r");
    int c, lines = 0;

    if (fp == NULL)
        return -1;
    while ((c = fgetc(fp)) != EOF)
        if (c == '\n')
            lines++;
    fclose(fp);
    return lines;
}

static int logger_usage(void)
{
    fprintf(stderr, "Usage: campuscompute logger [PRODUCERS [EVENTS_PER_PRODUCER [QUEUE_CAPACITY]]]\n"
                    "  PRODUCERS 1..8 (default 3), EVENTS 1..100 (default 6), CAPACITY 1..32 (default 4)\n");
    return 2;
}

int cmd_logger(int argc, char **argv)
{
    int producers = 3, events = 6, capacity = 4;
    LogQueue q;
    pthread_t consumer, tids[8];
    ProducerArg pargs[8];
    int started = 0, consumer_started = 0, rc = 1, expected, lines;

    if (argc > 3)
        return logger_usage();
    if ((argc > 0 && parse_int(argv[0], 1, 8, &producers) != 0) ||
        (argc > 1 && parse_int(argv[1], 1, 100, &events) != 0) ||
        (argc > 2 && parse_int(argv[2], 1, 32, &capacity) != 0)) {
        fprintf(stderr, "Error: invalid logger argument\n");
        return logger_usage();
    }

    memset(&q, 0, sizeof q);
    q.capacity = capacity;
    q.events_per_producer = events;
    q.buf = calloc((size_t)capacity, sizeof(LogEvent));
    if (q.buf == NULL) {
        perror("calloc");
        return 1;
    }
    if (pthread_mutex_init(&q.lock, NULL) != 0 || pthread_cond_init(&q.not_full, NULL) != 0 ||
        pthread_cond_init(&q.not_empty, NULL) != 0) {
        fprintf(stderr, "Error: cannot initialise mutex/condition variables\n");
        free(q.buf);
        return 1;
    }
    q.fp = fopen(LOG_FILE, "w");
    if (q.fp == NULL) {
        fprintf(stderr, "Error: cannot create '%s': %s\n", LOG_FILE, strerror(errno));
        free(q.buf);
        return 1;
    }

    printf("Logger demo: %d producers x %d events, queue capacity %d, log file '%s'\n",
           producers, events, capacity, LOG_FILE);

    if (pthread_create(&consumer, NULL, consumer_thread, &q) == 0) {
        consumer_started = 1;
        for (int i = 0; i < producers; i++) {
            pargs[i].q = &q;
            pargs[i].id = i + 1;
            if (pthread_create(&tids[i], NULL, producer_thread, &pargs[i]) != 0) {
                fprintf(stderr, "Error: pthread_create failed for producer %d\n", i + 1);
                break;
            }
            started++;
        }
    } else {
        fprintf(stderr, "Error: pthread_create failed for the consumer\n");
    }

    for (int i = 0; i < started; i++)                    /* wait for every producer */
        pthread_join(tids[i], NULL);
    printf("[MAIN] all %d producer threads joined\n", started);

    if (consumer_started) {
        pthread_mutex_lock(&q.lock);
        q.producers_finished = 1;                        /* tell the consumer to finish */
        pthread_cond_broadcast(&q.not_empty);
        pthread_mutex_unlock(&q.lock);
        pthread_join(consumer, NULL);
        printf("[MAIN] consumer thread joined\n");
    }

    fclose(q.fp);
    lines = count_lines(LOG_FILE);
    expected = producers * events;

    printf("\nLogger summary:\n");
    printf("  events expected          : %d\n", expected);
    printf("  events produced          : %d\n", q.produced);
    printf("  events consumed/logged   : %d\n", q.consumed);
    printf("  lines in %-16s: %d\n", LOG_FILE, lines);
    printf("  producer waits (full)    : %d\n", q.full_waits);
    printf("  consumer waits (empty)   : %d\n", q.empty_waits);
    printf("  threads created/joined   : %d/%d\n", started + consumer_started, started + consumer_started);

    if (started == producers && consumer_started && q.produced == expected &&
        q.consumed == expected && lines == expected) {
        printf("  RESULT: PASS - no event lost, all threads terminated cleanly\n");
        rc = 0;
    } else {
        printf("  RESULT: FAIL\n");
    }

    pthread_cond_destroy(&q.not_full);
    pthread_cond_destroy(&q.not_empty);
    pthread_mutex_destroy(&q.lock);
    free(q.buf);
    return rc;
}
