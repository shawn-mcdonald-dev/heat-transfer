#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <unistd.h>
#include "utilities.h"

#ifdef HAVE_MPI
#include <mpi.h>
#endif

enum { STENCIL_MPI_TAG_HALO_N = 7100, STENCIL_MPI_TAG_HALO_S = 7101 };

void print_matrix(double *data, int rows, int cols)
{
    for (int i = 0; i < rows; i++) {
        for (int j = 0; j < cols; j++) {
            printf("%5.2f ", data[i * cols + j]);
        }
        printf("\n");
    }
}

/**
 * Partition [0, mat_size) into num_nodes contiguous half-open ranges.
 * mat_size is a count (e.g. interior rows = rows - 2 for row-wise stencil).
 */
op_range_t get_node_op_range(int num_nodes, int node_idx, int mat_size)
{
    op_range_t range;

    range.start = (node_idx * mat_size) / num_nodes;
    range.stop = ((node_idx + 1) * mat_size) / num_nodes;
    return range;
}

/**
 * Split [node_start, node_start + block_size) among num_threads.
 * Returned range is half-open [start, stop) in the same coordinate system
 * as node_start (e.g. global interior index or local interior row index).
 */
op_range_t get_process_op_range(int num_threads, int thread_idx, int block_size,
                                int node_start)
{
    op_range_t range;
    int seg_start = (thread_idx * block_size) / num_threads;
    int seg_stop = ((thread_idx + 1) * block_size) / num_threads;

    range.start = node_start + seg_start;
    range.stop = node_start + seg_stop;
    return range;
}

/**
 * Check that divisions based on nodes and threads are correctly balanced.
 * size is the side length of a square interior; total cells = size * size.
 */
int check_balance_split(int nodes, int procs, uint64_t size)
{
    int *counts = malloc(sizeof(int) * (size_t)nodes * (size_t)procs);
    if (counts == NULL) {
        fprintf(stderr, "Memory allocation failed.\n");
        return 1;
    }
    int small = 1000000000;
    int large = 0;

    uint64_t total_cells = size * size;
    if (total_cells > (uint64_t)INT_MAX) {
        fprintf(stderr, "check_balance_split: size too large for int partition.\n");
        free(counts);
        return 1;
    }
    int mat_cells = (int)total_cells;

    for (int n = 0; n < nodes; n++) {
        op_range_t node_range = get_node_op_range(nodes, n, mat_cells);

        for (int p = 0; p < procs; p++) {
            op_range_t thread_range = get_process_op_range(
                procs,
                p,
                node_range.stop - node_range.start,
                node_range.start);

            counts[n * procs + p] = thread_range.stop - thread_range.start;
        }
    }

    for (int i = 0; i < nodes * procs; i++) {
        if (counts[i] > large) {
            large = counts[i];
        }
        if (counts[i] < small) {
            small = counts[i];
        }
    }

    free(counts);

    if (large - small > 1) {
        return 0;
    }
    return 1;
}

void get_procs_on_node(int *procs_on_node, int *node_rank)
{
#ifdef HAVE_MPI
    MPI_Comm node_comm;
    int err;

#if defined(MPI_COMM_TYPE_SHARED) && defined(MPI_VERSION) && (MPI_VERSION >= 3)
    err = MPI_Comm_split_type(MPI_COMM_WORLD, MPI_COMM_TYPE_SHARED, 0,
                              MPI_INFO_NULL, &node_comm);
#else
    /* Older MPI: cannot detect shared-memory peers; treat world as one node. */
    err = MPI_Comm_dup(MPI_COMM_WORLD, &node_comm);
#endif
    if (err != MPI_SUCCESS) {
        *procs_on_node = 1;
        *node_rank = 0;
        return;
    }

    MPI_Comm_rank(node_comm, node_rank);
    MPI_Comm_size(node_comm, procs_on_node);
    MPI_Comm_free(&node_comm);
#else
    (void)procs_on_node;
    (void)node_rank;
#endif
}

long get_pth_on_node(void)
{
    return sysconf(_SC_NPROCESSORS_ONLN);
}

void stencil_mpi_domain_init(stencil_mpi_domain_t *dom, int rows, int cols)
{
    if (dom == NULL || rows < 3 || cols < 3) {
        fprintf(stderr, "stencil_mpi_domain_init: invalid arguments.\n");
        return;
    }

    memset(dom, 0, sizeof(*dom));
    dom->rows = rows;
    dom->cols = cols;
    dom->interior_count = rows - 2;

#ifdef HAVE_MPI
    MPI_Comm_rank(MPI_COMM_WORLD, &dom->rank);
    MPI_Comm_size(MPI_COMM_WORLD, &dom->size);
#else
    dom->rank = 0;
    dom->size = 1;
#endif

    dom->interior = get_node_op_range(dom->size, dom->rank, dom->interior_count);
    dom->owned_rows = dom->interior.stop - dom->interior.start;

    /* Global interior rows are 1 .. rows-2; interior.start is offset in [0, interior_count). */
    dom->global_first_row = 1 + dom->interior.start;
    dom->global_last_row = dom->interior.stop; /* since stop is exclusive in offset space:
                                                  last global row = 1 + (stop-1) = stop */

    dom->local_rows = dom->owned_rows + 2;
    dom->interior_start_local = 1;
    dom->interior_end_local = dom->owned_rows + 1;

    dom->neighbor_north = (dom->rank > 0) ? dom->rank - 1 : -1;
    dom->neighbor_south = (dom->rank < dom->size - 1) ? dom->rank + 1 : -1;
}

void stencil_mpi_exchange_halos(stencil_mpi_domain_t *dom, double *curr)
{
    if (dom == NULL || curr == NULL || dom->size <= 1) {
        return;
    }

#ifdef HAVE_MPI
    MPI_Status st;

    /* North: recv neighbor's last interior row into our ghost row 0; send our first
     * interior row to neighbor's south ghost. */
    if (dom->neighbor_north >= 0) {
        MPI_Sendrecv(
            curr + (size_t)dom->interior_start_local * (size_t)dom->cols,
            dom->cols,
            MPI_DOUBLE,
            dom->neighbor_north,
            STENCIL_MPI_TAG_HALO_S,
            curr,
            dom->cols,
            MPI_DOUBLE,
            dom->neighbor_north,
            STENCIL_MPI_TAG_HALO_N,
            MPI_COMM_WORLD,
            &st);
    }

    /* South: recv neighbor's first interior row into our south ghost; send our last
     * interior row to neighbor's north ghost. */
    if (dom->neighbor_south >= 0) {
        int last_interior = dom->interior_end_local - 1;

        MPI_Sendrecv(
            curr + (size_t)last_interior * (size_t)dom->cols,
            dom->cols,
            MPI_DOUBLE,
            dom->neighbor_south,
            STENCIL_MPI_TAG_HALO_N,
            curr + (size_t)(dom->local_rows - 1) * (size_t)dom->cols,
            dom->cols,
            MPI_DOUBLE,
            dom->neighbor_south,
            STENCIL_MPI_TAG_HALO_S,
            MPI_COMM_WORLD,
            &st);
    }
#else
    (void)dom;
    (void)curr;
#endif
}

int stencil_mpi_global_interior_to_local(const stencil_mpi_domain_t *dom, int global_row)
{
    if (dom == NULL) {
        return -1;
    }
    if (global_row < dom->global_first_row || global_row > dom->global_last_row) {
        return -1;
    }
    return global_row - dom->global_first_row + dom->interior_start_local;
}

void stencil_mpi_local_thread_rows(const stencil_mpi_domain_t *dom, int num_threads,
                                   int thread_idx, int *start_row, int *end_row)
{
    if (dom == NULL || num_threads < 1 || thread_idx < 0 || thread_idx >= num_threads
        || start_row == NULL || end_row == NULL) {
        if (start_row) {
            *start_row = 1;
        }
        if (end_row) {
            *end_row = 0;
        }
        return;
    }

    op_range_t r = get_process_op_range(num_threads, thread_idx, dom->owned_rows,
                                        dom->interior_start_local);
    *start_row = r.start;
    *end_row = r.stop - 1;
}

int stencil_mpi_recombine_scatter_recvcount(int rank, int rows, int cols, int n_ranks)
{
    if (rows < 3 || cols < 1 || n_ranks < 1 || rank < 0 || rank >= n_ranks) {
        return 0;
    }
    op_range_t ir = get_node_op_range(n_ranks, rank, rows - 2);
    int owned = ir.stop - ir.start;
    int local_rows = owned + 2;
    return local_rows * cols;
}

int stencil_mpi_recombine_plan_create(stencil_mpi_recombine_plan_t *plan, int rows,
                                      int cols, int n_ranks)
{
    if (plan == NULL || rows < 3 || cols < 1 || n_ranks < 1) {
        return -1;
    }

    memset(plan, 0, sizeof(*plan));
    plan->rows = rows;
    plan->cols = cols;
    plan->n_ranks = n_ranks;

    plan->scatter_sendcounts = calloc((size_t)n_ranks, sizeof(int));
    plan->scatter_displs = calloc((size_t)n_ranks, sizeof(int));
    plan->gather_recvcounts = calloc((size_t)n_ranks, sizeof(int));
    plan->gather_displs = calloc((size_t)n_ranks, sizeof(int));

    if (plan->scatter_sendcounts == NULL || plan->scatter_displs == NULL
        || plan->gather_recvcounts == NULL || plan->gather_displs == NULL) {
        stencil_mpi_recombine_plan_destroy(plan);
        return -1;
    }
    return 0;
}

void stencil_mpi_recombine_plan_destroy(stencil_mpi_recombine_plan_t *plan)
{
    if (plan == NULL) {
        return;
    }
    free(plan->scatter_sendcounts);
    free(plan->scatter_displs);
    free(plan->gather_recvcounts);
    free(plan->gather_displs);
    memset(plan, 0, sizeof(*plan));
}

void stencil_mpi_recombine_plan_fill(stencil_mpi_recombine_plan_t *plan)
{
    if (plan == NULL || plan->scatter_sendcounts == NULL) {
        return;
    }

    int rows = plan->rows;
    int cols = plan->cols;
    int n = plan->n_ranks;

    for (int r = 0; r < n; r++) {
        op_range_t ir = get_node_op_range(n, r, rows - 2);
        int owned = ir.stop - ir.start;
        int lr = owned + 2;
        int gfr = 1 + ir.start;

        plan->scatter_sendcounts[r] = lr * cols;
        plan->scatter_displs[r] = (gfr - 1) * cols;
        plan->gather_recvcounts[r] = owned * cols;
        plan->gather_displs[r] = gfr * cols;
    }
}

int stencil_mpi_shard_write(const stencil_mpi_domain_t *dom, const double *curr,
                            const char *path_stem)
{
    if (dom == NULL || curr == NULL || path_stem == NULL) {
        return -1;
    }

    int local_start;
    int local_end_inclusive;
    int file_global_first_row;

    if (dom->size == 1) {
        local_start = 0;
        local_end_inclusive = dom->local_rows - 1;
        file_global_first_row = 0;
    } else if (dom->rank == 0) {
        local_start = 0;
        local_end_inclusive = dom->owned_rows;
        file_global_first_row = 0;
    } else if (dom->rank == dom->size - 1) {
        local_start = 1;
        local_end_inclusive = dom->local_rows - 1;
        file_global_first_row = dom->global_first_row;
    } else {
        local_start = 1;
        local_end_inclusive = dom->owned_rows;
        file_global_first_row = dom->global_first_row;
    }

    int file_n_rows = local_end_inclusive - local_start + 1;
    if (file_n_rows < 1 || dom->cols < 1) {
        return -1;
    }

    char path[4096];
    if (snprintf(path, sizeof(path), "%s.%d", path_stem, dom->rank) >= (int)sizeof(path)) {
        fprintf(stderr, "stencil_mpi_shard_write: path too long.\n");
        return -1;
    }

    FILE *fp = fopen(path, "wb");
    if (fp == NULL) {
        perror("stencil_mpi_shard_write fopen");
        return -1;
    }

    uint32_t magic = STENCIL_MPI_SHARD_MAGIC;
    int32_t version = STENCIL_MPI_SHARD_VERSION;
    int32_t gr = dom->rows;
    int32_t gc = dom->cols;
    int32_t rk = dom->rank;
    int32_t ws = dom->size;
    int32_t fg = file_global_first_row;
    int32_t nr = file_n_rows;

    if (fwrite(&magic, sizeof(magic), 1, fp) != 1
        || fwrite(&version, sizeof(version), 1, fp) != 1
        || fwrite(&gr, sizeof(gr), 1, fp) != 1
        || fwrite(&gc, sizeof(gc), 1, fp) != 1
        || fwrite(&rk, sizeof(rk), 1, fp) != 1
        || fwrite(&ws, sizeof(ws), 1, fp) != 1
        || fwrite(&fg, sizeof(fg), 1, fp) != 1
        || fwrite(&nr, sizeof(nr), 1, fp) != 1) {
        fprintf(stderr, "stencil_mpi_shard_write: header write failed.\n");
        fclose(fp);
        return -1;
    }

    for (int li = local_start; li <= local_end_inclusive; li++) {
        const double *row = curr + (size_t)li * (size_t)dom->cols;
        if (fwrite(row, sizeof(double), (size_t)dom->cols, fp) != (size_t)dom->cols) {
            fprintf(stderr, "stencil_mpi_shard_write: data write failed.\n");
            fclose(fp);
            return -1;
        }
    }

    if (fclose(fp) != 0) {
        perror("stencil_mpi_shard_write fclose");
        return -1;
    }
    return 0;
}
