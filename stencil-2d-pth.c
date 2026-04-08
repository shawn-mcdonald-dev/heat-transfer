#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <sys/time.h>
#include "utilities.h"

/* Timing macro */
#define GET_TIME(now) {                 \
    struct timeval t;                  \
    gettimeofday(&t, NULL);             \
    now = t.tv_sec + t.tv_usec / 1e6;   \
}

typedef struct {
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    int count;
    int trip_count;
} barrier_t;

typedef struct {
    int tid;
    int start_row;
    int end_row;
} thread_arg_t;

static int rows, cols, num_iters, num_threads;
static int debug = 0;
static double *curr = NULL;
static double *next = NULL;
static barrier_t barrier;

/* ---------- Custom barrier for macOS ---------- */
void barrier_init(barrier_t *b, int n) {
    pthread_mutex_init(&b->mutex, NULL);
    pthread_cond_init(&b->cond, NULL);
    b->count = 0;
    b->trip_count = n;
}

void barrier_wait(barrier_t *b) {
    pthread_mutex_lock(&b->mutex);

    b->count++;

    if (b->count == b->trip_count) {
        b->count = 0;
        pthread_cond_broadcast(&b->cond);
    } else {
        pthread_cond_wait(&b->cond, &b->mutex);
    }

    pthread_mutex_unlock(&b->mutex);
}

void barrier_destroy(barrier_t *b) {
    pthread_mutex_destroy(&b->mutex);
    pthread_cond_destroy(&b->cond);
}

static void *worker(void *arg) {
    thread_arg_t *targ = (thread_arg_t *)arg;

    for (int iter = 0; iter < num_iters; iter++) {
        /* Compute assigned interior rows only */
        for (int i = targ->start_row; i <= targ->end_row; i++) {
            for (int j = 1; j < cols - 1; j++) {
                next[i * cols + j] = (
                    curr[(i - 1) * cols + (j - 1)] +  /* NW */
                    curr[(i - 1) * cols + j] +        /* N  */
                    curr[(i - 1) * cols + (j + 1)] +  /* NE */
                    curr[i * cols + (j + 1)] +        /* E  */
                    curr[(i + 1) * cols + (j + 1)] +  /* SE */
                    curr[(i + 1) * cols + j] +        /* S  */
                    curr[(i + 1) * cols + (j - 1)] +  /* SW */
                    curr[i * cols + (j - 1)] +        /* W  */
                    curr[i * cols + j]                /* M  */
                ) / 9.0;
            }
        }

        /* Wait until all threads finish computing */
        barrier_wait(&barrier);

        /* One thread handles boundaries, debug print, and swap */
        if (targ->tid == 0) {
            /* Copy left/right boundaries */
            for (int i = 0; i < rows; i++) {
                next[i * cols + 0] = curr[i * cols + 0];
                next[i * cols + (cols - 1)] = curr[i * cols + (cols - 1)];
            }

            /* Copy top/bottom boundaries */
            for (int j = 0; j < cols; j++) {
                next[0 * cols + j] = curr[0 * cols + j];
                next[(rows - 1) * cols + j] = curr[(rows - 1) * cols + j];
            }

            if (debug == 2) {
                printf("Iteration %d\n", iter + 1);
                print_matrix(next, rows, cols);
            }

            /* Swap curr and next */
            double *temp = curr;
            curr = next;
            next = temp;
        }

        /* Wait until swap is finished */
        barrier_wait(&barrier);
    }

    return NULL;
}

/* ---------- Main ---------- */
int main(int argc, char *argv[]) {

    double overall_start, overall_end;
    double compute_start, compute_end;
    GET_TIME(overall_start);

    char *infile = NULL;
    char *outfile = NULL;

    for (int k = 1; k < argc; k++) {
        if (strcmp(argv[k], "-t") == 0 && k + 1 < argc) {
            num_iters = atoi(argv[++k]);
        } else if (strcmp(argv[k], "-i") == 0 && k + 1 < argc) {
            infile = argv[++k];
        } else if (strcmp(argv[k], "-o") == 0 && k + 1 < argc) {
            outfile = argv[++k];
        } else if (strcmp(argv[k], "-p") == 0 && k + 1 < argc) {
            num_threads = atoi(argv[++k]);
        } else if (strcmp(argv[k], "-v") == 0 && k + 1 < argc) {
            debug = atoi(argv[++k]);
        } else {
            fprintf(stderr,
                    "usage: %s -t <num_iters> -i <in> -o <out> -p <num_threads> [-v <debug>]\n",
                    argv[0]);
            return 1;
        }
    }

    if (num_iters < 0 || infile == NULL || outfile == NULL || num_threads <= 0) {
        fprintf(stderr,
                "usage: %s -t <num_iters> -i <in> -o <out> -p <num_threads> [-v <debug>]\n",
                argv[0]);
        return 1;
    }

    FILE *fp = fopen(infile, "rb");
    if (fp == NULL) {
        perror("fopen input");
        return 1;
    }

    if (fread(&rows, sizeof(int), 1, fp) != 1 ||
        fread(&cols, sizeof(int), 1, fp) != 1) {
        fprintf(stderr, "Error reading dimensions.\n");
        fclose(fp);
        return 1;
    }

    curr = malloc(rows * cols * sizeof(double));
    next = malloc(rows * cols * sizeof(double));
    if (curr == NULL || next == NULL) {
        fprintf(stderr, "Memory allocation failed.\n");
        fclose(fp);
        free(curr);
        free(next);
        return 1;
    }

    if (fread(curr, sizeof(double), rows * cols, fp) != (size_t)(rows * cols)) {
        fprintf(stderr, "Error reading matrix data.\n");
        fclose(fp);
        free(curr);
        free(next);
        return 1;
    }
    fclose(fp);

    /* Start next as a copy of curr so boundaries are valid */
    memcpy(next, curr, rows * cols * sizeof(double));

    pthread_t *threads = malloc(num_threads * sizeof(pthread_t));
    thread_arg_t *args = malloc(num_threads * sizeof(thread_arg_t));
    if (threads == NULL || args == NULL) {
        fprintf(stderr, "Memory allocation failed.\n");
        free(curr);
        free(next);
        free(threads);
        free(args);
        return 1;
    }

    barrier_init(&barrier, num_threads);

    int interior_rows = rows - 2;
    int base = interior_rows / num_threads;
    int extra = interior_rows % num_threads;

    GET_TIME(compute_start);

    int current_row = 1;
    for (int tid = 0; tid < num_threads; tid++) {
        int count = base + (tid < extra ? 1 : 0);

        args[tid].tid = tid;
        args[tid].start_row = current_row;
        args[tid].end_row = current_row + count - 1;

        current_row += count;

        if (pthread_create(&threads[tid], NULL, worker, &args[tid]) != 0) {
            fprintf(stderr, "Error creating thread %d\n", tid);
            barrier_destroy(&barrier);
            free(curr);
            free(next);
            free(threads);
            free(args);
            return 1;
        }
    }

    for (int tid = 0; tid < num_threads; tid++) {
        pthread_join(threads[tid], NULL);
    }

    GET_TIME(compute_end);

    if (debug >= 1) {
        printf("rows=%d cols=%d iterations=%d threads=%d\n",
               rows, cols, num_iters, num_threads);
        printf("input=%s output=%s\n", infile, outfile);
    }

    fp = fopen(outfile, "wb");
    if (fp == NULL) {
        perror("fopen output");
        barrier_destroy(&barrier);
        free(curr);
        free(next);
        free(threads);
        free(args);
        return 1;
    }

    fwrite(&rows, sizeof(int), 1, fp);
    fwrite(&cols, sizeof(int), 1, fp);
    fwrite(curr, sizeof(double), rows * cols, fp);
    fclose(fp);

    barrier_destroy(&barrier);
    free(curr);
    free(next);
    free(threads);
    free(args);

    GET_TIME(overall_end);

    printf("Overall time: %f seconds\n", overall_end - overall_start);
    printf("Compute time: %f seconds\n", compute_end - compute_start);

    return 0;
}