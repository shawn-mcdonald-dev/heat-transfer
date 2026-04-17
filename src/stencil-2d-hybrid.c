#include <mpi.h>
#include <omp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include "utilities.h"
 

#define GET_TIME(now) {                  \
    struct timeval t;                    \
    gettimeofday(&t, NULL);              \
    now = t.tv_sec + t.tv_usec / 1e6;   \
}
 
static void apply_boundaries_to_next(const stencil_mpi_domain_t *dom,
                                     const double *curr, double *next)
{
    int cols = dom->cols;
    int lr   = dom->local_rows;
 
    for (int li = 0; li < lr; li++) {
        size_t base = (size_t)li * (size_t)cols;
        next[base]              = curr[base];
        next[base + cols - 1]   = curr[base + cols - 1];
    }

    if (dom->rank == 0) {
        for (int j = 0; j < cols; j++)
            next[j] = curr[j];
    }

    if (dom->rank == dom->size - 1) {
        int last = lr - 1;
        size_t base = (size_t)last * (size_t)cols;
        for (int j = 0; j < cols; j++)
            next[base + j] = curr[base + j];
    }
}
 
static void apply_interior_stencil_omp(const stencil_mpi_domain_t *dom,
                                       const double *curr, double *next)
{
    int cols = dom->cols;

    #pragma omp parallel for schedule(static)
    for (int li = dom->interior_start_local; li < dom->interior_end_local; li++) {
        for (int j = 1; j < cols - 1; j++) {
            size_t o = (size_t)li * (size_t)cols + (size_t)j;
            next[o] = (
                curr[o - (size_t)cols - 1] +  /* NW */
                curr[o - (size_t)cols    ] +  /* N  */
                curr[o - (size_t)cols + 1] +  /* NE */
                curr[o + 1               ] +  /* E  */
                curr[o + (size_t)cols + 1] +  /* SE */
                curr[o + (size_t)cols    ] +  /* S  */
                curr[o + (size_t)cols - 1] +  /* SW */
                curr[o - 1               ] +  /* W  */
                curr[o                   ]    /* M  */
            ) / 9.0;
        }
    }
}

 
int main(int argc, char *argv[])
{
    double overall_start, overall_end;
    double compute_start, compute_end;
    GET_TIME(overall_start);
 
    int provided;
    MPI_Init_thread(&argc, &argv, MPI_THREAD_FUNNELED, &provided);
    if (provided < MPI_THREAD_FUNNELED) {
        fprintf(stderr,
                "ERROR: MPI does not support MPI_THREAD_FUNNELED "
                "(got level %d, need %d).\n",
                provided, MPI_THREAD_FUNNELED);
        MPI_Abort(MPI_COMM_WORLD, 1);
    }
 
    int world_rank, world_size;
    MPI_Comm_rank(MPI_COMM_WORLD, &world_rank);
    MPI_Comm_size(MPI_COMM_WORLD, &world_size);
 
    int    num_iters  = -1;
    int    num_threads = 1;
    char  *infile  = NULL;
    char  *outfile = NULL;
 
    for (int k = 1; k < argc; k++) {
        if (strcmp(argv[k], "-t") == 0 && k + 1 < argc) {
            num_iters   = atoi(argv[++k]);
        } else if (strcmp(argv[k], "-i") == 0 && k + 1 < argc) {
            infile      = argv[++k];
        } else if (strcmp(argv[k], "-o") == 0 && k + 1 < argc) {
            outfile     = argv[++k];
        } else if (strcmp(argv[k], "-p") == 0 && k + 1 < argc) {
            num_threads = atoi(argv[++k]);
        } else {
            if (world_rank == 0)
                fprintf(stderr,
                        "usage: mpirun -np <ranks> %s "
                        "-t <iters> -i <in> -o <out_stem> -p <threads>\n",
                        argv[0]);
            MPI_Finalize();
            return 1;
        }
    }
 
    if (num_iters < 0 || infile == NULL || outfile == NULL) {
        if (world_rank == 0)
            fprintf(stderr,
                    "usage: mpirun -np <ranks> %s "
                    "-t <iters> -i <in> -o <out_stem> -p <threads>\n",
                    argv[0]);
        MPI_Finalize();
        return 1;
    }
 
    omp_set_num_threads(num_threads);
 
    int     rows    = 0;
    int     cols    = 0;
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
            fprintf(stderr, "malloc readbuf failed.\n");
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
        if (world_rank == 0)
            fprintf(stderr, "Grid must be at least 3x3.\n");
        free(readbuf);
        MPI_Finalize();
        return 1;
    }
    if (world_size > rows - 2) {
        if (world_rank == 0)
            fprintf(stderr,
                    "Too many MPI ranks (%d) for %d interior rows.\n",
                    world_size, rows - 2);
        free(readbuf);
        MPI_Finalize();
        return 1;
    }
 
    stencil_mpi_domain_t dom;
    stencil_mpi_domain_init(&dom, rows, cols);
 
    stencil_mpi_recombine_plan_t plan;
    memset(&plan, 0, sizeof(plan));
    if (stencil_mpi_recombine_plan_create(&plan, rows, cols, world_size) != 0) {
        if (world_rank == 0)
            fprintf(stderr, "stencil_mpi_recombine_plan_create failed.\n");
        free(readbuf);
        MPI_Finalize();
        return 1;
    }
    stencil_mpi_recombine_plan_fill(&plan);

    int    scatter_recv  = stencil_mpi_recombine_scatter_recvcount(
                               world_rank, rows, cols, world_size);
    size_t scatter_bytes = (size_t)scatter_recv * sizeof(double);
 
    double *curr = malloc(scatter_bytes);
    double *next = malloc(scatter_bytes);
    if (curr == NULL || next == NULL) {
        if (world_rank == 0)
            fprintf(stderr, "malloc curr/next failed.\n");
        free(curr); free(next); free(readbuf);
        stencil_mpi_recombine_plan_destroy(&plan);
        MPI_Finalize();
        return 1;
    }
 
    MPI_Scatterv(
        world_rank == 0 ? readbuf              : NULL,
        world_rank == 0 ? plan.scatter_sendcounts : NULL,
        world_rank == 0 ? plan.scatter_displs     : NULL,
        MPI_DOUBLE,
        curr, scatter_recv, MPI_DOUBLE,
        0, MPI_COMM_WORLD);
 
    stencil_mpi_recombine_plan_destroy(&plan);
 
    if (world_rank == 0) {
        free(readbuf);
        readbuf = NULL;
    }
 
    memcpy(next, curr, scatter_bytes);
 
    
    GET_TIME(compute_start);
 
    for (int iter = 0; iter < num_iters; iter++) {
 
        stencil_mpi_exchange_halos(&dom, curr);

        apply_boundaries_to_next(&dom, curr, next);
 
        apply_interior_stencil_omp(&dom, curr, next);

        double *tmp = curr;
        curr = next;
        next = tmp;
    }
 
    GET_TIME(compute_end);
 
    MPI_Barrier(MPI_COMM_WORLD);
    if (stencil_mpi_serial_gather_write(&dom, curr, outfile) != 0) {
        if (world_rank == 0)
            fprintf(stderr, "stencil_mpi_serial_gather_write failed.\n");
        free(curr); free(next);
        MPI_Abort(MPI_COMM_WORLD, 1);
    }
 
    if (world_rank == 0) {
        GET_TIME(overall_end);
        printf("ranks=%d threads_per_rank=%d rows=%d cols=%d iters=%d\n",
               world_size, num_threads, rows, cols, num_iters);
        printf("T_overall=%f T_computation=%f T_other=%f\n",
               overall_end - overall_start,
               compute_end - compute_start,
               (overall_end - overall_start) - (compute_end - compute_start));
    }
 
    free(curr);
    free(next);
    MPI_Finalize();
    return 0;
}