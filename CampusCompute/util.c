/*
 * util.c - small helpers shared by all modules
 */
#include "campuscompute.h"
#include <ctype.h>
#include <time.h>

/* Parse a whole string as an integer in [min, max]. Returns 0 on success. */
int parse_int(const char *text, long min, long max, int *out)
{
    char *end = NULL;
    long value;

    if (text == NULL || *text == '\0')
        return -1;

    errno = 0;
    value = strtol(text, &end, 10);
    if (errno == ERANGE || end == text || *end != '\0' ||
        value < min || value > max)
        return -1;

    *out = (int)value;
    return 0;
}

/* Parse a line holding exactly 'want' integers. Returns 0 on success. */
int parse_int_line(const char *line, long *vals, int want)
{
    const char *p = line;
    char *end;

    for (int k = 0; k < want; k++) {
        while (isspace((unsigned char)*p))
            p++;
        errno = 0;
        vals[k] = strtol(p, &end, 10);
        if (end == p || errno == ERANGE)
            return -1;
        p = end;
    }
    while (isspace((unsigned char)*p))
        p++;
    return (*p == '\0') ? 0 : -1;      /* reject trailing garbage */
}

/*
 * Read every integer in a text file into vals[]. '#' starts a comment.
 * Returns 0 on success; prints a message with the line number on error.
 */
int load_int_tokens(const char *path, long *vals, int max_vals, int *count)
{
    char line[512];
    int lineno = 0;
    FILE *fp = fopen(path, "r");

    if (fp == NULL) {
        fprintf(stderr, "Error: cannot open '%s': %s\n", path, strerror(errno));
        return -1;
    }

    *count = 0;
    while (fgets(line, sizeof line, fp) != NULL) {
        const char *p = line;
        char *hash = strchr(line, '#');

        lineno++;
        if (hash != NULL)
            *hash = '\0';

        for (;;) {
            char *end;
            long v;

            while (isspace((unsigned char)*p))
                p++;
            if (*p == '\0')
                break;

            errno = 0;
            v = strtol(p, &end, 10);
            if (end == p || errno == ERANGE) {
                fprintf(stderr, "Error: '%s' line %d: invalid number near '%.12s'\n",
                        path, lineno, p);
                fclose(fp);
                return -1;
            }
            if (*count >= max_vals) {
                fprintf(stderr, "Error: '%s' line %d: too many values\n", path, lineno);
                fclose(fp);
                return -1;
            }
            vals[(*count)++] = v;
            p = end;
        }
    }

    fclose(fp);
    return 0;
}

void sleep_ms(long ms)
{
    struct timespec ts;

    ts.tv_sec  = ms / 1000;
    ts.tv_nsec = (ms % 1000) * 1000000L;
    while (nanosleep(&ts, &ts) == -1 && errno == EINTR)
        ;                               /* resume after a signal */
}

double monotonic_seconds(void)
{
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

void print_banner(const char *title)
{
    printf("\n==============================================================\n");
    printf(" %s\n", title);
    printf("==============================================================\n");
}
