#include <omp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void print_matrix(double *data, int rows, int cols) {
    for (int i = 0; i < rows; i++) {
        for (int j = 0; j < cols; j++) {
            printf("%5.2f ", data[i * cols + j]);
        }
        printf("\n");
    }
}

int main(int argc, char *argv[]) {
    int num_iters = -1;
    char *infile = NULL;
    char *outfile = NULL;
    int debug = 0;

    for (int k = 1; k < argc; k++) {
        if (strcmp(argv[k], "-n") == 0 && k + 1 < argc) {
            num_iters = atoi(argv[++k]);
        } else if (strcmp(argv[k], "-i") == 0 && k + 1 < argc) {
            infile = argv[++k];
        } else if (strcmp(argv[k], "-o") == 0 && k + 1 < argc) {
            outfile = argv[++k];
        } else if (strcmp(argv[k], "-v") == 0 && k + 1 < argc) {
            debug = atoi(argv[++k]);
        } else {
            fprintf(stderr, "usage: %s -n <num_iters> -i <in> -o <out> -v <debug>\n", argv[0]);
            return 1;
        }
    }

    if (num_iters < 0 || infile == NULL || outfile == NULL) {
        fprintf(stderr, "usage: %s -n <num_iters> -i <in> -o <out> -v <debug>\n", argv[0]);
        return 1;
    }

    FILE *fp = fopen(infile, "rb");
    if (fp == NULL) {
        perror("fopen input");
        return 1;
    }

    int rows, cols;
    if (fread(&rows, sizeof(int), 1, fp) != 1 ||
        fread(&cols, sizeof(int), 1, fp) != 1) {
        fprintf(stderr, "Error reading dimensions.\n");
        fclose(fp);
        return 1;
    }

    double *curr = malloc(rows * cols * sizeof(double));
    double *next = malloc(rows * cols * sizeof(double));
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

    for (int iter = 0; iter < num_iters; iter++) {
#pragma omp parallel
        {
#pragma omp for schedule(static) nowait
            for (int i = 0; i < rows; i++) {
                next[i * cols + 0] = curr[i * cols + 0];
                next[i * cols + (cols - 1)] = curr[i * cols + (cols - 1)];
            }
#pragma omp for schedule(static) nowait
            for (int j = 0; j < cols; j++) {
                next[0 * cols + j] = curr[0 * cols + j];
                next[(rows - 1) * cols + j] = curr[(rows - 1) * cols + j];
            }
#pragma omp for schedule(static) collapse(2)
            for (int i = 1; i < rows - 1; i++) {
                for (int j = 1; j < cols - 1; j++) {
                    next[i * cols + j] = (
                        curr[(i - 1) * cols + (j - 1)] +
                        curr[(i - 1) * cols + j] +
                        curr[(i - 1) * cols + (j + 1)] +
                        curr[i * cols + (j + 1)] +
                        curr[(i + 1) * cols + (j + 1)] +
                        curr[(i + 1) * cols + j] +
                        curr[(i + 1) * cols + (j - 1)] +
                        curr[i * cols + (j - 1)] +
                        curr[i * cols + j]
                    ) / 9.0;
                }
            }
        }

        if (debug == 2) {
            printf("Iteration %d\n", iter + 1);
            print_matrix(next, rows, cols);
        }

        double *temp = curr;
        curr = next;
        next = temp;
    }

    if (debug >= 1) {
        printf("rows=%d cols=%d iterations=%d\n", rows, cols, num_iters);
        printf("input=%s output=%s\n", infile, outfile);
    }

    fp = fopen(outfile, "wb");
    if (fp == NULL) {
        perror("fopen output");
        free(curr);
        free(next);
        return 1;
    }

    fwrite(&rows, sizeof(int), 1, fp);
    fwrite(&cols, sizeof(int), 1, fp);
    fwrite(curr, sizeof(double), rows * cols, fp);

    fclose(fp);
    free(curr);
    free(next);

    return 0;
}
