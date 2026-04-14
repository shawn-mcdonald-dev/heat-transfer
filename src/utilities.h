#ifndef UTILITIES_H
#define UTILITIES_H

#include <pthread.h>
#include <stdint.h>

typedef __uint64_t uint64_t;

typedef struct op_range
{
    int start;
    int stop;
} op_range_t;

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

typedef struct {
    int tid;
    int start_idx;
    int end_idx;
} hybrid_thread_arg_t;

/**
 * MPI row-wise domain for the 2D stencil (one rank owns a contiguous band of
 * global interior rows). Ghost rows live at local row 0 (north) and
 * local row local_rows - 1 (south). Interior rows are local indices
 * [interior_start_local, interior_end_local) — i.e. stop is exclusive.
 *
 * MPI_Init must have been called before stencil_mpi_domain_init when built
 * with HAVE_MPI.
 */
typedef struct stencil_mpi_domain {
    int rank;
    int size;
    int rows;
    int cols;
    int interior_count;     /* rows - 2 (global interior row count) */
    op_range_t interior;    /* offsets into [0, interior_count): [start, stop) */
    int global_first_row;   /* first global interior row this rank owns (1..rows-2) */
    int global_last_row;    /* last global interior row (inclusive) */
    int owned_rows;         /* interior rows owned by this rank */
    int local_rows;         /* owned_rows + 2 (north/south ghost lines) */
    int interior_start_local; /* first owned interior row index in local buffer (always 1) */
    int interior_end_local;   /* exclusive end (== owned_rows + 1) */
    int neighbor_north;     /* rank - 1, or -1 */
    int neighbor_south;     /* rank + 1, or -1 */
} stencil_mpi_domain_t;

void print_matrix(double *data, int rows, int cols);

op_range_t get_node_op_range(int num_nodes, int node_idx, int mat_size);

op_range_t get_process_op_range(int num_threads, int thread_idx, int block_size,
                                int node_start);

int check_balance_split(int nodes, int procs, uint64_t size);

void stencil_mpi_domain_init(stencil_mpi_domain_t *dom, int rows, int cols);

void stencil_mpi_exchange_halos(stencil_mpi_domain_t *dom, double *curr);

/**
 * Map global interior row index (1 .. rows-2) to local row index for this rank.
 * Returns -1 if that row is not owned by this rank.
 */
int stencil_mpi_global_interior_to_local(const stencil_mpi_domain_t *dom, int global_row);

/**
 * Thread slice of local interior rows [1, owned_rows] for hybrid pthread use.
 * Uses get_process_op_range with node_start = interior_start_local and
 * block_size = owned_rows. Returned range uses inclusive end_row for pthread code.
 */
void stencil_mpi_local_thread_rows(const stencil_mpi_domain_t *dom, int num_threads,
                                   int thread_idx, int *start_row, int *end_row);

/** Per-rank shard format (little-endian); merge with merge-stencil-shards.py */
#define STENCIL_MPI_SHARD_MAGIC UINT32_C(0x31485453) /* "STH\1" */
#define STENCIL_MPI_SHARD_VERSION 1

/**
 * MPI_Scatterv metadata (counts/displs in MPI_DOUBLE elements). Used only for
 * distributing the initial global matrix from rank 0.
 */
typedef struct stencil_mpi_recombine_plan {
    int n_ranks;
    int rows;
    int cols;
    int *scatter_sendcounts;
    int *scatter_displs;
    int *gather_recvcounts;
    int *gather_displs;
} stencil_mpi_recombine_plan_t;

int stencil_mpi_recombine_plan_create(stencil_mpi_recombine_plan_t *plan, int rows,
                                      int cols, int n_ranks);

void stencil_mpi_recombine_plan_destroy(stencil_mpi_recombine_plan_t *plan);

void stencil_mpi_recombine_plan_fill(stencil_mpi_recombine_plan_t *plan);

int stencil_mpi_recombine_scatter_recvcount(int rank, int rows, int cols, int n_ranks);

/**
 * Write this rank's rows to path_stem.<rank> (e.g. out.dat.3).
 * No MPI communication; safe to call before/after timing sections.
 * Returns 0 on success, -1 on I/O error.
 */
int stencil_mpi_shard_write(const stencil_mpi_domain_t *dom, const double *curr,
                            const char *path_stem);

#endif
