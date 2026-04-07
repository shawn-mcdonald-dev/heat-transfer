#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

typedef struct {
    int tid;
    int start_row;
    int end_row;
} thread_arg_t;

static int rows, cols, num_iters, num_threads;
static int debug = 0;
static double *curr = NULL;
static double *next = NULL;
static pthread_barrier_t barrier;

static void print_matrix(double *data, int rows, int cols) {
    for (int i = 0; i < rows; i++) {
        for (int j = 0; j < cols; j++) {
            printf("%5.2f ", data[i * cols + j]);
        }
        printf("\n");
    }
}

static void *worker(void *arg) {
    thread_arg_t *targ = (thread_arg_t *)arg;

    for (int iter = 0; iter < num_iters; iter++) {
        // Compute assigned interior rows
        for (int i = targ->start_row; i <= targ->end_row; i++) {
            for (int j = 1; j < cols - 1; j++) {
                next[i * cols + j] = (
                    curr[(i - 1) * cols + (j - 1)] +  // NW
                    curr[(i - 1) * cols + j] +        // N
                    curr[(i - 1) * cols + (j + 1)] +  // NE
                    curr[i * cols + (j + 1)] +        // E
                    curr[(i + 1) * cols + (j + 1)] +  // SE
                    curr[(i + 1) * cols + j] +        // S
                    curr[(i + 1) * cols + (j - 1)] +  // SW
                    curr[i * cols + (j - 1)] +        // W
                    curr[i * cols + j]                // M
                ) / 9.0;
            }
        }

        // Wait until all threads finish computing
        pthread_barrier_wait(&barrier);

        // One thread copies boundaries and swaps arrays
        if (targ->tid == 0) {
            for (int i = 0; i < rows; i++) {
                next[i * cols + 0] = curr[i * cols + 0];
                next[i * cols + (cols - 1)] = curr[i * cols + (cols - 1)];
            }
            for (int j = 0; j < cols; j++) {
                next[0 * cols + j] = curr[0 * cols + j];
                next[(rows - 1) * cols + j] = curr[(rows - 1) * cols + j];
            }

            if (debug == 2) {
                printf("Iteration %d\n", iter + 1);
                print_matrix(next, rows, cols);
            }

            double *temp = curr;
            curr = next;
            next = temp;
        }

        // Wait until swap is done before next iteration
        pthread_barrier_wait(&barrier);
    }

    return NULL;
}

int main(int argc, char *argv[]) {
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
            fprintf(stderr, "usage: %s -t <num_iters> -i <in> -o <out> -p <num_threads> [-v <debug>]\n", argv[0]);
            return 1;
        }
    }

    if (num_iters < 0 || infile == NULL || outfile == NULL || num_threads <= 0) {
        fprintf(stderr, "usage: %s -t <num_iters> -i <in> -o <out> -p <num_threads> [-v <debug>]\n", argv[0]);
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

    // Initialize next to curr so boundaries start valid
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

    pthread_barrier_init(&barrier, NULL, num_threads);

    int interior_rows = rows - 2;
    int base = interior_rows / num_threads;
    int extra = interior_rows % num_threads;

    int current_row = 1;
    for (int tid = 0; tid < num_threads; tid++) {
        int count = base + (tid < extra ? 1 : 0);

        args[tid].tid = tid;
        args[tid].start_row = current_row;
        args[tid].end_row = current_row + count - 1;

        current_row += count;

        if (pthread_create(&threads[tid], NULL, worker, &args[tid]) != 0) {
            fprintf(stderr, "Error creating thread %d\n", tid);
            free(curr);
            free(next);
            free(threads);
            free(args);
            pthread_barrier_destroy(&barrier);
            return 1;
        }
    }

    for (int tid = 0; tid < num_threads; tid++) {
        pthread_join(threads[tid], NULL);
    }

    if (debug >= 1) {
        printf("rows=%d cols=%d iterations=%d threads=%d\n", rows, cols, num_iters, num_threads);
        printf("input=%s output=%s\n", infile, outfile);
    }

    fp = fopen(outfile, "wb");
    if (fp == NULL) {
        perror("fopen output");
        free(curr);
        free(next);
        free(threads);
        free(args);
        pthread_barrier_destroy(&barrier);
        return 1;
    }

    fwrite(&rows, sizeof(int), 1, fp);
    fwrite(&cols, sizeof(int), 1, fp);
    fwrite(curr, sizeof(double), rows * cols, fp);
    fclose(fp);

    pthread_barrier_destroy(&barrier);
    free(curr);
    free(next);
    free(threads);
    free(args);

    return 0;
}