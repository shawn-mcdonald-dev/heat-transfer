#include <mpi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "utilities.h"

static void apply_boundaries_to_next(const stencil_mpi_domain_t *dom, const double *curr,
                                     double *next)
{
    int cols = dom->cols;
    int lr = dom->local_rows;

    for (int li = 0; li < lr; li++) {
        size_t base = (size_t)li * (size_t)cols;
        next[base + 0] = curr[base + 0];
        next[base + (size_t)cols - 1] = curr[base + (size_t)cols - 1];
    }

    if (dom->rank == 0) {
        for (int j = 0; j < cols; j++) {
            next[j] = curr[j];
        }
    }

    if (dom->rank == dom->size - 1) {
        int last = lr - 1;
        size_t base = (size_t)last * (size_t)cols;
        for (int j = 0; j < cols; j++) {
            next[base + (size_t)j] = curr[base + (size_t)j];
        }
    }
}

static void apply_interior_stencil(const stencil_mpi_domain_t *dom, const double *curr,
                                   double *next)
{
    int cols = dom->cols;
    for (int li = dom->interior_start_local; li < dom->interior_end_local; li++) {
        for (int j = 1; j < cols - 1; j++) {
            size_t o = (size_t)li * (size_t)cols + (size_t)j;
            next[o] = (
                curr[o - (size_t)cols - 1] + curr[o - (size_t)cols] + curr[o - (size_t)cols + 1] +
                curr[o + (size_t)cols - 1] + curr[o + (size_t)cols] + curr[o + (size_t)cols + 1] +
                curr[o - 1] + curr[o + 1] + curr[o]
            ) / 9.0;
        }
    }
}

int main(int argc, char *argv[])
{
    MPI_Init(&argc, &argv);

    int world_rank;
    int world_size;
    MPI_Comm_rank(MPI_COMM_WORLD, &world_rank);
    MPI_Comm_size(MPI_COMM_WORLD, &world_size);

    int num_iters = -1;
    char *infile = NULL;
    char *outfile = NULL;

    for (int k = 1; k < argc; k++) {
        if (strcmp(argv[k], "-n") == 0 && k + 1 < argc) {
            num_iters = atoi(argv[++k]);
        } else if (strcmp(argv[k], "-i") == 0 && k + 1 < argc) {
            infile = argv[++k];
        } else if (strcmp(argv[k], "-o") == 0 && k + 1 < argc) {
            outfile = argv[++k];
        } else {
            if (world_rank == 0) {
                fprintf(stderr,
                        "usage: %s -n <num_iters> -i <in> -o <out.dat>\n"
                        "  Writes one binary file (same format as stencil-2d: int rows, cols,\n"
                        "  then rows*cols doubles).\n",
                        argv[0]);
            }
            MPI_Finalize();
            return 1;
        }
    }

    if (num_iters < 0 || infile == NULL || outfile == NULL) {
        if (world_rank == 0) {
            fprintf(stderr,
                    "usage: %s -n <num_iters> -i <in> -o <out.dat>\n",
                    argv[0]);
        }
        MPI_Finalize();
        return 1;
    }

    int rows = 0;
    int cols = 0;
    double *readbuf = NULL;

    if (world_rank == 0) {
        FILE *fp = fopen(infile, "rb");
        if (fp == NULL) {
            perror("fopen input");
            MPI_Abort(MPI_COMM_WORLD, 1);
        }
        if (fread(&rows, sizeof(int), 1, fp) != 1 ||
            fread(&cols, sizeof(int), 1, fp) != 1) {
            fprintf(stderr, "Error reading dimensions.\n");
            fclose(fp);
            MPI_Abort(MPI_COMM_WORLD, 1);
        }
        readbuf = malloc((size_t)rows * (size_t)cols * sizeof(double));
        if (readbuf == NULL) {
            fprintf(stderr, "Memory allocation failed.\n");
            fclose(fp);
            MPI_Abort(MPI_COMM_WORLD, 1);
        }
        if (fread(readbuf, sizeof(double), (size_t)rows * (size_t)cols, fp)
            != (size_t)rows * (size_t)cols) {
            fprintf(stderr, "Error reading matrix data.\n");
            free(readbuf);
            fclose(fp);
            MPI_Abort(MPI_COMM_WORLD, 1);
        }
        fclose(fp);
    }

    MPI_Bcast(&rows, 1, MPI_INT, 0, MPI_COMM_WORLD);
    MPI_Bcast(&cols, 1, MPI_INT, 0, MPI_COMM_WORLD);

    if (rows < 3 || cols < 3) {
        if (world_rank == 0) {
            fprintf(stderr, "Invalid grid size (need rows,cols >= 3).\n");
        }
        free(readbuf);
        MPI_Finalize();
        return 1;
    }

    if (world_size > rows - 2) {
        if (world_rank == 0) {
            fprintf(stderr,
                    "MPI_Comm_size (%d) must be <= interior rows (%d).\n",
                    world_size,
                    rows - 2);
        }
        free(readbuf);
        MPI_Finalize();
        return 1;
    }

    stencil_mpi_domain_t dom;
    stencil_mpi_domain_init(&dom, rows, cols);

    stencil_mpi_recombine_plan_t plan;
    memset(&plan, 0, sizeof(plan));
    if (stencil_mpi_recombine_plan_create(&plan, rows, cols, world_size) != 0) {
        if (world_rank == 0) {
            fprintf(stderr, "stencil_mpi_recombine_plan_create failed.\n");
        }
        free(readbuf);
        MPI_Finalize();
        return 1;
    }
    stencil_mpi_recombine_plan_fill(&plan);

    int scatter_recv = stencil_mpi_recombine_scatter_recvcount(
        world_rank, rows, cols, world_size);
    size_t scatter_bytes = (size_t)scatter_recv * sizeof(double);

    double *curr = malloc(scatter_bytes);
    double *next = malloc(scatter_bytes);
    if (curr == NULL || next == NULL) {
        if (world_rank == 0) {
            fprintf(stderr, "Memory allocation failed.\n");
        }
        free(curr);
        free(next);
        free(readbuf);
        stencil_mpi_recombine_plan_destroy(&plan);
        MPI_Finalize();
        return 1;
    }

    MPI_Scatterv(
        world_rank == 0 ? readbuf : NULL,
        world_rank == 0 ? plan.scatter_sendcounts : NULL,
        world_rank == 0 ? plan.scatter_displs : NULL,
        MPI_DOUBLE,
        curr,
        scatter_recv,
        MPI_DOUBLE,
        0,
        MPI_COMM_WORLD);

    stencil_mpi_recombine_plan_destroy(&plan);

    if (world_rank == 0) {
        free(readbuf);
        readbuf = NULL;
    }

    memcpy(next, curr, scatter_bytes);

    for (int iter = 0; iter < num_iters; iter++) {
        /* TIMING: iteration start */
        stencil_mpi_exchange_halos(&dom, curr);
        apply_boundaries_to_next(&dom, curr, next);
        apply_interior_stencil(&dom, curr, next);

        double *tmp = curr;
        curr = next;
        next = tmp;
        /* TIMING: iteration end */
    }

    MPI_Barrier(MPI_COMM_WORLD);
    if (stencil_mpi_serial_gather_write(&dom, curr, outfile) != 0) {
        if (world_rank == 0) {
            fprintf(stderr, "stencil_mpi_serial_gather_write failed (see rank stderr).\n");
        }
        free(curr);
        free(next);
        MPI_Abort(MPI_COMM_WORLD, 1);
    }

    free(curr);
    free(next);
    MPI_Finalize();
    return 0;
}
